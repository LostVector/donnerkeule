// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// GPU producer -> four NHI rails -> GPU verifier, in both directions.
// TCP carries only topology, readiness and completion records (no payload).
#include <hip/hip_runtime.h>
#include <hip/hip_ext.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../include/tbv_gpu_direct.h"
#include "../../proto/gpu_pool.h"

static_assert(sizeof(tbv_gpu_import) == 96);
static_assert(sizeof(tbv_gpu_transfer) == 24);
static_assert(sizeof(tbv_gpu_wait) == 32);
static_assert(sizeof(tbv_gpu_topology) == 648);

static void check(bool ok, const char *what)
{
	if (!ok)
		throw std::runtime_error(std::string(what) + ": " + strerror(errno));
}

static void hip_check(hipError_t status, const char *what)
{
	if (status != hipSuccess)
		throw std::runtime_error(std::string(what) + ": " + hipGetErrorString(status));
}
#define HIP(call) hip_check((call), #call)

struct Fd {
	int fd = -1;
	Fd() = default;
	explicit Fd(int value) : fd(value) {}
	Fd(const Fd &) = delete;
	Fd &operator=(const Fd &) = delete;
	~Fd() { if (fd >= 0) close(fd); }
};

struct Pool {
	void *data = nullptr;
	Fd exported;
	~Pool() {
		if (data) {
			(void)hipDeviceSynchronize();
			if (exported.fd >= 0) {
				close(exported.fd);
				exported.fd = -1;
			}
			(void)hipFree(data);
		}
	}
	void allocate(size_t bytes, bool uncached) {
		if (uncached)
			HIP(hipExtMallocWithFlags(&data, bytes, hipDeviceMallocUncached));
		else
			HIP(hipMalloc(&data, bytes));
		hipDeviceptr_t base = 0;
		size_t size = 0;
		HIP(hipMemGetAddressRange(&base, &size, reinterpret_cast<hipDeviceptr_t>(data)));
		if (reinterpret_cast<void *>(base) != data || size != bytes)
			throw std::runtime_error("pool must be a dedicated, exact-size HIP allocation");
		HIP(hipMemset(data, 0xa5, bytes));
		HIP(hipDeviceSynchronize());
		HIP(hipMemGetHandleForAddressRange(&exported.fd,
			reinterpret_cast<hipDeviceptr_t>(data), bytes,
			hipMemRangeHandleTypeDmaBufFd, 0));
	}
};

struct Gpu {
	hipStream_t stream = nullptr;
	hipEvent_t fence = nullptr;
	unsigned long long *result = nullptr;
	~Gpu() {
		if (stream) (void)hipStreamSynchronize(stream);
		if (result) (void)hipFree(result);
		if (fence) (void)hipEventDestroy(fence);
		if (stream) (void)hipStreamDestroy(stream);
	}
	void init() {
		HIP(hipStreamCreate(&stream));
		// Timing enabled: force a real marker, including when the stream is idle.
		HIP(hipEventCreateWithFlags(&fence, hipEventReleaseToSystem));
		HIP(hipMalloc(&result, 2 * sizeof(*result)));
	}
	void release() {
		HIP(hipEventRecord(fence, stream));
		HIP(hipEventSynchronize(fence));
	}
};

__device__ static uint32_t pattern(uint32_t word, uint32_t generation, uint32_t rank)
{
	uint32_t x = word ^ (generation * 0x9e3779b9u) ^ (rank * 0x85ebca6bu);
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	return x ^ (x >> 16);
}

__global__ static void produce(uint32_t *dst, uint32_t words, uint32_t generation,
			      uint32_t rank)
{
	for (uint32_t i = blockIdx.x * blockDim.x + threadIdx.x; i < words;
	     i += blockDim.x * gridDim.x)
		dst[i] = pattern(i, generation, rank);
}

__global__ static void consume(const uint32_t *src, uint32_t words,
			      uint32_t generation, uint32_t rank,
			      unsigned long long *result, bool verify)
{
	unsigned long long bad = 0, sum = 0;
	for (uint32_t i = blockIdx.x * blockDim.x + threadIdx.x; i < words;
	     i += blockDim.x * gridDim.x) {
		uint32_t value = src[i];
		bad += verify && value != pattern(i, generation, rank);
		sum += value;
	}
	atomicAdd(result, bad);
	atomicAdd(result + 1, sum);
}

static void socket_options(int fd)
{
	int yes = 1;
	timeval timeout{30, 0};
	check(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == 0,
	      "TCP_NODELAY");
	check(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0,
	      "SO_RCVTIMEO");
	check(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0,
	      "SO_SNDTIMEO");
}

static int connect_peer(const char *host, unsigned port)
{
	addrinfo hints{}, *addresses = nullptr;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	std::string service = std::to_string(port);
	int ret = getaddrinfo(host, service.c_str(), &hints, &addresses);
	if (ret) throw std::runtime_error(gai_strerror(ret));
	int fd = -1;
	for (unsigned attempt = 0; attempt < 100 && fd < 0; attempt++) {
		for (auto *a = addresses; a; a = a->ai_next) {
			fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
			if (fd >= 0 && connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
			if (fd >= 0) close(fd);
			fd = -1;
		}
		if (fd < 0) usleep(100000);
	}
	freeaddrinfo(addresses);
	check(fd >= 0, "connect");
	return fd;
}

static int accept_peer(unsigned port)
{
	Fd listener(socket(AF_INET, SOCK_STREAM, 0));
	check(listener.fd >= 0, "socket");
	int yes = 1;
	check(setsockopt(listener.fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == 0,
	      "SO_REUSEADDR");
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(port);
	check(bind(listener.fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0,
	      "bind");
	check(listen(listener.fd, 1) == 0, "listen");
	printf("LISTEN port=%u\n", port);
	fflush(stdout);
	int fd = accept(listener.fd, nullptr, nullptr);
	check(fd >= 0, "accept");
	return fd;
}

static void write_all(int fd, const void *buf, size_t bytes)
{
	auto *p = static_cast<const char *>(buf);
	while (bytes) {
		ssize_t n = send(fd, p, bytes, MSG_NOSIGNAL);
		if (n < 0 && errno == EINTR) continue;
		check(n > 0, "control send");
		bytes -= n;
		p += n;
	}
}

static void read_all(int fd, void *buf, size_t bytes)
{
	auto *p = static_cast<char *>(buf);
	while (bytes) {
		ssize_t n = recv(fd, p, bytes, 0);
		if (n < 0 && errno == EINTR) continue;
		if (n == 0) throw std::runtime_error("control peer disconnected");
		check(n > 0, "control receive");
		bytes -= n;
		p += n;
	}
}

static void barrier(int fd, uint64_t sequence, uint64_t bytes, uint64_t offset,
		    uint64_t phase)
{
	// Test protocol is deliberately fixed to Linux x86-64 on both peers.
	std::array<uint64_t, 4> local{sequence, bytes, offset, phase}, remote{};
	write_all(fd, local.data(), sizeof(local));
	read_all(fd, remote.data(), sizeof(remote));
	if (remote != local) throw std::runtime_error("control phase/sequence/shape mismatch");
}

static void wait_dma(int fd, uint32_t direction, uint64_t sequence, uint64_t bytes)
{
	tbv_gpu_wait wait{};
	wait.direction = direction;
	wait.timeout_ms = 10000;
	wait.sequence = sequence;
	check(ioctl(fd, TBV_GPU_WAIT, &wait) == 0, "TBV_GPU_WAIT");
	if (wait.status || wait.completed_bytes != bytes)
		throw std::runtime_error("DMA completion failed: status=" +
			std::to_string(wait.status) + " bytes=" + std::to_string(wait.completed_bytes));
}

static void expect_error(int fd, unsigned long command, void *arg, int expected)
{
	errno = 0;
	int ret = ioctl(fd, command, arg);
	if (ret != -1 || errno != expected)
		throw std::runtime_error("negative ioctl: expected errno=" +
			std::to_string(expected) + " got ret=" + std::to_string(ret) +
			" errno=" + std::to_string(errno));
}

static void probe_import(int fd, const tbv_gpu_import &valid, bool timeout_test)
{
	Fd second(open("/dev/tbv_gpu_direct", O_RDWR | O_CLOEXEC));
	if (second.fd >= 0 || errno != EBUSY)
		throw std::runtime_error("exclusive-open check failed");
	unsigned cases = 1;
	auto bad = valid;
	bad.version++;
	expect_error(fd, TBV_GPU_IMPORT, &bad, EINVAL); cases++;
	bad = valid; bad.reserved = 1;
	expect_error(fd, TBV_GPU_IMPORT, &bad, EINVAL); cases++;
	bad = valid; bad.count = 0;
	expect_error(fd, TBV_GPU_IMPORT, &bad, EINVAL); cases++;
	bad = valid; bad.bytes--;
	expect_error(fd, TBV_GPU_IMPORT, &bad, EINVAL); cases++;
	bad = valid; bad.bytes += 4096;
	expect_error(fd, TBV_GPU_IMPORT, &bad, EINVAL); cases++;
	bad = valid; bad.tx_fd = -1;
	expect_error(fd, TBV_GPU_IMPORT, &bad, EBADF); cases++;
	bad = valid; bad.rx_fd = -1;
	expect_error(fd, TBV_GPU_IMPORT, &bad, EBADF); cases++;
	bad = valid; bad.rx_fd = bad.tx_fd;
	expect_error(fd, TBV_GPU_IMPORT, &bad, EINVAL); cases++;
	if (valid.count > 1) {
		bad = valid; bad.rails[1] = bad.rails[0];
		expect_error(fd, TBV_GPU_IMPORT, &bad, EINVAL); cases++;
		bad = valid; bad.rails[1].peer = UINT32_MAX;
		expect_error(fd, TBV_GPU_IMPORT, &bad, ENOTCONN); cases++;
	}
	check(ioctl(fd, TBV_GPU_IMPORT, &valid) == 0, "IMPORT after rollback tests");
	bad = valid;
	expect_error(fd, TBV_GPU_IMPORT, &bad, EBUSY); cases++;
	tbv_gpu_transfer transfer{0, 0, 4096};
	expect_error(fd, TBV_GPU_PREPARE_RX, &transfer, EINVAL); cases++;
	transfer = {1, 1, 4096};
	expect_error(fd, TBV_GPU_PREPARE_RX, &transfer, EINVAL); cases++;
	transfer = {1, valid.bytes, 4096};
	expect_error(fd, TBV_GPU_PREPARE_RX, &transfer, EINVAL); cases++;
	transfer = {1, 0, 0};
	expect_error(fd, TBV_GPU_SUBMIT_TX, &transfer, EINVAL); cases++;
	if (!timeout_test) {
		printf("PASS import/map/close probe, %u negative checks (no descriptors)\n", cases);
		return;
	}
	transfer = {1, 0, 4096};
	check(ioctl(fd, TBV_GPU_PREPARE_RX, &transfer) == 0, "timeout PREPARE_RX");
	expect_error(fd, TBV_GPU_PREPARE_RX, &transfer, EBUSY); cases++;
	tbv_gpu_wait wait{};
	wait.direction = TBV_GPU_RX;
	// Exercise multiple watchdog rescans without a sender: none may fake
	// a completion, and the deadline must still abort and drain RX.
	wait.timeout_ms = 50;
	wait.sequence = 1;
	check(ioctl(fd, TBV_GPU_WAIT, &wait) == 0, "timeout WAIT");
	if (wait.status != -ETIMEDOUT || wait.completed_bytes)
		throw std::runtime_error("RX without peer did not fail closed on timeout");
	expect_error(fd, TBV_GPU_SUBMIT_TX, &transfer, ENOTCONN); cases++;
	printf("PASS timeout/drain/abort/close, %u negative checks\n", cases);
}

static tbv_gpu_import pair_rails(const tbv_gpu_topology &local,
				const tbv_gpu_topology &remote, unsigned rank,
				unsigned rails, int tx_fd, int rx_fd, size_t bytes)
{
	if (local.version != TBV_GPU_ABI_VERSION || remote.version != TBV_GPU_ABI_VERSION ||
	    local.count > TBV_GPU_MAX_RAILS || remote.count > TBV_GPU_MAX_RAILS ||
	    local.count < rails || remote.count < rails)
		throw std::runtime_error("not enough ready GPU-direct rails on both peers");
	tbv_gpu_import req{};
	req.version = TBV_GPU_ABI_VERSION;
	req.count = rails;
	req.tx_fd = tx_fd;
	req.rx_fd = rx_fd;
	req.bytes = bytes;
	// Rank 0 sets canonical order; match physical link UUIDs and HopIDs,
	// never assume Linux domain numbers or cable order match across hosts.
	const auto &leader = rank == 0 ? local : remote;
	const auto &follower = rank == 0 ? remote : local;
	for (unsigned i = 0; i < rails; i++) {
		const auto &a = leader.rails[i];
		bool found = false;
		for (unsigned j = 0; j < follower.count; j++) {
			const auto &b = follower.rails[j];
			if (memcmp(a.local_domain, b.remote_domain, 16) ||
			    memcmp(a.remote_domain, b.local_domain, 16) ||
			    memcmp(a.local_host, b.remote_host, 16) ||
			    memcmp(a.remote_host, b.local_host, 16) ||
			    a.local_path != b.remote_path || a.remote_path != b.local_path)
				continue;
			req.rails[i] = rank == 0 ? a.id : b.id;
			printf("rail[%u]=peer%u/%u local_path=%u remote_path=%u\n", i,
			       req.rails[i].peer, req.rails[i].rail,
			       rank == 0 ? a.local_path : b.local_path,
			       rank == 0 ? a.remote_path : b.remote_path);
			found = true;
			break;
		}
		if (!found) throw std::runtime_error("cannot pair physical rails");
	}
	return req;
}

int main(int argc, char **argv)
{
	try {
		unsigned rank = 2, iterations = 64, rails = 4, port = 19875;
		bool uncached = false, prewarm = true, probe = false, timeout_test = false;
		const char *host = nullptr;
		std::vector<size_t> sizes{16384, 65536, 266240, 1048576, 4194304};
		for (int i = 1; i < argc; i++) {
			auto number = [&]() -> unsigned {
				if (++i >= argc) throw std::runtime_error("missing argument");
				char *end = nullptr;
				unsigned long n = strtoul(argv[i], &end, 10);
				if (!*argv[i] || *end || n > UINT32_MAX)
					throw std::runtime_error("invalid number");
				return n;
			};
			if (!strcmp(argv[i], "--rank")) rank = number();
			else if (!strcmp(argv[i], "--iterations")) iterations = number();
			else if (!strcmp(argv[i], "--rails")) rails = number();
			else if (!strcmp(argv[i], "--port")) port = number();
			else if (!strcmp(argv[i], "--bytes")) sizes = {number()};
			else if (!strcmp(argv[i], "--connect") && i + 1 < argc) host = argv[++i];
			else if (!strcmp(argv[i], "--uncached")) uncached = true;
			else if (!strcmp(argv[i], "--no-prewarm")) prewarm = false;
			else if (!strcmp(argv[i], "--probe")) probe = true;
			else if (!strcmp(argv[i], "--timeout-test")) probe = timeout_test = true;
			else if (!strcmp(argv[i], "--help")) {
				puts("tbv_gpu_direct --rank 0|1 [--connect HOST] [--port N] [--rails N]\n"
				     "  [--iterations N] [--bytes N] [--uncached] [--no-prewarm] [--probe|--timeout-test]\n"
				     "Default: 64 exchanges per size, 16KiB/64KiB/260KiB/1MiB/4MiB, four rails.\n"
				     "--probe checks rollback and imports without any descriptors.\n"
				     "--timeout-test also arms RX without a sender, checks abort/drain; reload afterwards.");
				return 0;
			} else throw std::runtime_error("unknown argument (use --help)");
		}
		if ((!probe && (rank > 1 || (rank == 1 && !host))) || !iterations ||
		    !rails || rails > TBV_GPU_MAX_RAILS || !port || port > 65535)
			throw std::runtime_error("invalid rank/peer/iterations/rails/port");
		size_t max_bytes = *std::max_element(sizes.begin(), sizes.end());
		size_t window = (max_bytes + 65535) & ~size_t(65535);
		// The ROCm allocator can round exported BOs to 2 MiB even when
		// hipMemGetAddressRange reports the smaller requested size.
		// Export a whole dedicated BO, including that allocation granule.
		constexpr size_t granule = 2 << 20;
		size_t pool_bytes = (3 * window + granule - 1) & ~(granule - 1);
		for (size_t size : sizes)
			if (tbv_gpu_check_transfer(pool_bytes, 0, size, rails))
				throw std::runtime_error("size must be whole 4096-byte frames, <=512 frames/rail");
		HIP(hipSetDevice(0));
		hipDeviceProp_t prop{};
		HIP(hipGetDeviceProperties(&prop, 0));
		int runtime = 0;
		HIP(hipRuntimeGetVersion(&runtime));
		printf("GPU=%s arch=%s HIP=%d pool_bytes=%zu memory=%s prewarm=%u\n",
		       prop.name, prop.gcnArchName, runtime, pool_bytes,
		       uncached ? "uncached" : "coarse", prewarm);
		Pool tx, rx;
		tx.allocate(pool_bytes, uncached);
		rx.allocate(pool_bytes, uncached);
		Gpu gpu;
		gpu.init();
		// Declared after pools: close/drain importer before HIP frees storage.
		Fd device(open("/dev/tbv_gpu_direct", O_RDWR | O_CLOEXEC));
		check(device.fd >= 0, "open /dev/tbv_gpu_direct");
		tbv_gpu_topology local{};
		for (unsigned attempt = 0; attempt < 100; attempt++) {
			check(ioctl(device.fd, TBV_GPU_GET_TOPOLOGY, &local) == 0, "GET_TOPOLOGY");
			if (local.count >= rails) break;
			usleep(100000);
		}
		if (probe) {
			if (local.count < rails) throw std::runtime_error("not enough rails for probe");
			tbv_gpu_import req{};
			req.version = TBV_GPU_ABI_VERSION;
			req.count = rails;
			req.tx_fd = tx.exported.fd;
			req.rx_fd = rx.exported.fd;
			req.bytes = pool_bytes;
			for (unsigned i = 0; i < rails; i++) req.rails[i] = local.rails[i].id;
			probe_import(device.fd, req, timeout_test);
			return 0;
		}
		Fd control(rank == 0 ? accept_peer(port) : connect_peer(host, port));
		socket_options(control.fd);
		tbv_gpu_topology remote{};
		write_all(control.fd, &local, sizeof(local));
		read_all(control.fd, &remote, sizeof(remote));
		auto req = pair_rails(local, remote, rank, rails, tx.exported.fd,
				      rx.exported.fd, pool_bytes);
		check(ioctl(device.fd, TBV_GPU_IMPORT, &req) == 0, "IMPORT");
		barrier(control.fd, iterations, rails, pool_bytes, 0);
		uint64_t sequence = 0;
		for (size_t bytes : sizes) {
			double transport_us = 0, total_us = 0;
			unsigned long long total_bad = 0, checksum = 0;
			for (unsigned it = 0; it < iterations; it++) {
				++sequence;
				size_t offset = ((sequence - 1) % 3) * window;
				auto *send = reinterpret_cast<uint32_t *>(static_cast<char *>(tx.data) + offset);
				auto *recv = reinterpret_cast<uint32_t *>(static_cast<char *>(rx.data) + offset);
				auto begin = std::chrono::steady_clock::now();
				hipLaunchKernelGGL(produce, dim3(64), dim3(256), 0, gpu.stream,
						   send, bytes / 4, sequence, rank);
				HIP(hipGetLastError());
				if (prewarm) {
					HIP(hipMemsetAsync(gpu.result, 0, 16, gpu.stream));
					hipLaunchKernelGGL(consume, dim3(64), dim3(256), 0, gpu.stream,
							   recv, bytes / 4, 0, 0, gpu.result, false);
					HIP(hipGetLastError());
				}
				gpu.release();
				tbv_gpu_transfer transfer{sequence, offset, bytes};
				check(ioctl(device.fd, TBV_GPU_PREPARE_RX, &transfer) == 0, "PREPARE_RX");
				barrier(control.fd, sequence, bytes, offset, 1);
				auto start_dma = std::chrono::steady_clock::now();
				check(ioctl(device.fd, TBV_GPU_SUBMIT_TX, &transfer) == 0, "SUBMIT_TX");
				wait_dma(device.fd, TBV_GPU_TX, sequence, bytes);
				wait_dma(device.fd, TBV_GPU_RX, sequence, bytes);
				auto end_dma = std::chrono::steady_clock::now();
				// RX acquire prototype from strix-rdma. Qualify this exact ROCm
				// build with cache-hot generations; do not infer a portable HIP guarantee.
				gpu.release();
				HIP(hipMemsetAsync(gpu.result, 0, 16, gpu.stream));
				hipLaunchKernelGGL(consume, dim3(64), dim3(256), 0, gpu.stream,
						   recv, bytes / 4, sequence, 1 - rank, gpu.result, true);
				HIP(hipGetLastError());
				HIP(hipStreamSynchronize(gpu.stream));
				unsigned long long result[2]{};
				// Only the 16-byte GPU result is read by CPU, never the tensor.
				HIP(hipMemcpy(result, gpu.result, sizeof(result), hipMemcpyDeviceToHost));
				total_bad += result[0];
				checksum ^= result[1];
				if (result[0]) throw std::runtime_error("GPU payload mismatch: " + std::to_string(result[0]));
				barrier(control.fd, sequence, bytes, offset, 2);
				auto end = std::chrono::steady_clock::now();
				transport_us += std::chrono::duration<double, std::micro>(end_dma - start_dma).count();
				total_us += std::chrono::duration<double, std::micro>(end - begin).count();
			}
			printf("PASS rank=%u bytes=%zu exchanges=%u rails=%u bad_words=%llu checksum=%llx "
			       "dma_us=%.2f verified_exchange_us=%.2f local_tx_GBps=%.3f\n",
			       rank, bytes, iterations, rails, total_bad, checksum,
			       transport_us / iterations, total_us / iterations,
			       double(bytes) * iterations / (transport_us * 1000));
			fflush(stdout);
		}
		barrier(control.fd, sequence, 0, 0, 3);
		return 0;
	} catch (const std::exception &e) {
		fprintf(stderr, "FAIL: %s\n", e.what());
		return 1;
	}
}
