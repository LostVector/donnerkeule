// GPU producer -> production communicator -> already queued GPU consumer.
// Runs on both ranks; TCP carries metadata only. No model is loaded.
#include <hip/hip_runtime.h>
#include <sys/resource.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "src/models/qwen38_flash_next/kernels/rocm/donnerkeule.hpp"
#include "src/models/qwen38_flash_next/kernels/rocm/kernels.hpp"
#include "src/models/qwen38_flash_next/kernels/rocm/verbs.hpp"

namespace q = gufo::models::qwen38_flash_next::rocm;
namespace {
__host__ __device__ float Pattern(std::size_t i, unsigned rank,
                                  unsigned generation) {
  return static_cast<float>((i * 13 + generation * 97 + rank * 131) % 1024) *
         0.125F;
}
__global__ void Produce(float* data, std::size_t count, unsigned rank,
                        unsigned generation) {
  for (std::size_t i = blockIdx.x * blockDim.x + threadIdx.x; i < count;
       i += blockDim.x * gridDim.x)
    data[i] = Pattern(i, rank, generation);
}
// Keep a real pre-DMA read observable, including every cache line of RX.
__global__ void Prewarm(const float* data, std::size_t count, float* scratch) {
  float sum = 0;
  for (std::size_t i = blockIdx.x * blockDim.x + threadIdx.x; i < count;
       i += blockDim.x * gridDim.x)
    sum += data[i];
  scratch[blockIdx.x * blockDim.x + threadIdx.x] = sum;
}
__global__ void Verify(const float* data, std::size_t count,
                       unsigned generation, unsigned* errors) {
  for (std::size_t i = blockIdx.x * blockDim.x + threadIdx.x; i < count;
       i += blockDim.x * gridDim.x) {
    const float expected =
        Pattern(i, 0, generation) + Pattern(i, 1, generation);
    if (data[i] != expected)
      atomicAdd(errors, 1U);
  }
}
bool Hip(hipError_t status, const char* operation) {
  if (status == hipSuccess)
    return true;
  std::fprintf(stderr, "%s: %s\n", operation, hipGetErrorString(status));
  return false;
}
double CpuUs() {
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  return (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1e6 +
         usage.ru_utime.tv_usec + usage.ru_stime.tv_usec;
}
}  // namespace
int main(int argc, char** argv) {
  unsigned rank = 0, iterations = 128, mismatch = 0;
  std::string transport = "donnerkeule", host, rdma_device;
  for (int i = 1; i < argc; ++i) {
    if (i + 1 == argc)
      return 2;
    const std::string arg = argv[i++], value = argv[i];
    if (arg == "--rank")
      rank = std::stoul(value);
    else if (arg == "--connect")
      host = value;
    else if (arg == "--iterations")
      iterations = std::stoul(value);
    else if (arg == "--transport")
      transport = value;
    else if (arg == "--rdma-device")
      rdma_device = value;
    else if (arg == "--mismatch")
      mismatch = std::stoul(value);
    else
      return 2;
  }
  if (rank > 1 || !iterations || iterations > 4096 || mismatch > 1 ||
      (mismatch && transport != "donnerkeule") ||
      (transport != "donnerkeule" && transport != "verbs"))
    return 2;
  std::string error;
  std::shared_ptr<q::Communicator> comm;
  if (transport == "donnerkeule") {
    comm = q::CreateDonnerkeuleCommunicator(
        {.rank = rank, .bootstrap_host = host, .bootstrap_port = 18625},
        &error);
  } else {
    comm = q::CreateIbrverbsCommunicator({.rank = rank,
                                          .world_size = 2,
                                          .bootstrap_host = host,
                                          .bootstrap_port = 18625,
                                          .rdma_device = rdma_device,
                                          .gid_index = std::nullopt},
                                         &error);
  }
  if (!comm) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  std::printf("%s\n", comm->Describe().c_str());
  std::fflush(stdout);
  hipStream_t stream{};
  float *local = nullptr, *output = nullptr, *scratch = nullptr;
  unsigned* errors = nullptr;
  constexpr std::size_t kMax = 32U << 20;
  if (!Hip(hipStreamCreate(&stream), "stream") ||
      !Hip(hipMalloc(&local, kMax), "local") ||
      !Hip(hipMalloc(&output, kMax), "output") ||
      !Hip(hipMalloc(&scratch, 64 * 256 * sizeof(float)), "scratch") ||
      !Hip(hipMalloc(&errors, sizeof(unsigned)), "errors"))
    return 1;
  const std::vector<std::size_t> sizes = {4,       16384,   65536,   266244,
                                          1048576, 4194304, 8388612, kMax - 64};
  bool ok = true;
  unsigned generation = 0;
  for (std::size_t bytes : sizes) {
    const std::size_t count = bytes / sizeof(float);
    if (!comm->BeginOperation(bytes, &error)) {
      ok = false;
      break;
    }
    if (!Hip(hipMemsetAsync(errors, 0, sizeof(unsigned), stream),
             "clear errors")) {
      ok = false;
      break;
    }
    // Warm this payload shape before the measured group, retaining correctness.
    for (unsigned phase = 0; phase != 2 && ok; ++phase) {
      const unsigned repeats = phase == 0 ? 8 : iterations;
      const double cpu = CpuUs();
      const auto begin = std::chrono::steady_clock::now();
      for (unsigned iteration = 0; iteration != repeats && ok; ++iteration) {
        q::Communicator::QueuedPartial lease;
        if (!comm->QueuePartial(bytes + (mismatch && rank ? 4 : 0), &lease,
                                &error)) {
          ok = false;
          break;
        }
        const unsigned gen = ++generation;
        // Both paths exercise the same cache prewarm and independent check.
        Prewarm<<<64, 256, 0, stream>>>(lease.peer, count, scratch);
        Produce<<<64, 256, 0, stream>>>(
            comm->DirectOutput() ? lease.send : local, count, rank, gen);
        if (comm->DirectOutput()) {
          if (lease.wire_bytes > bytes)
            ok = Hip(hipMemsetAsync(reinterpret_cast<char*>(lease.send) + bytes,
                                    0, lease.wire_bytes - bytes, stream),
                     "tail");
          ok = ok &&
               Hip(hipEventRecord(lease.release, stream), "system release");
          q::PublishPartial(lease.ready, lease.value, stream);
        } else {
          q::StagePartial(local, lease.send, count, lease.arrivals, lease.ready,
                          lease.value, stream);
        }
        q::WaitValue(lease.arrived, lease.value, lease.wait_ticks,
                     lease.timed_out, stream);
        q::SumPartials(comm->DirectOutput() ? lease.send : local, lease.peer,
                       output, count, stream);
        Verify<<<64, 256, 0, stream>>>(output, count, gen, errors);
        // Keep the host far enough ahead that the GPU wait is already queued,
        // while bounding the communicator queue for longer runs.
        if ((iteration + 1) % 32 == 0)
          ok = ok && Hip(hipStreamSynchronize(stream), "group sync") &&
               comm->CheckQueued(&error);
      }
      ok = ok && Hip(hipStreamSynchronize(stream), "sync") &&
           comm->CheckQueued(&error);
      const double wall = std::chrono::duration<double, std::micro>(
                              std::chrono::steady_clock::now() - begin)
                              .count();
      const double cpu_us = CpuUs() - cpu;
      unsigned mismatches = 0;
      ok = ok && Hip(hipMemcpy(&mismatches, errors, sizeof(mismatches),
                               hipMemcpyDeviceToHost),
                     "read check");
      if (mismatches) {
        std::fprintf(stderr, "bytes=%zu mismatched_words=%u\n", bytes,
                     mismatches);
        ok = false;
      }
      if (phase == 1 && ok) {
        std::printf(
            "rank=%u transport=%s bytes=%zu iterations=%u exchange_us=%.3f "
            "payload_GBps=%.3f process_cpu_us=%.3f cpu_cores=%.3f errors=0\n",
            rank, transport.c_str(), bytes, repeats, wall / repeats,
            bytes * repeats / wall / 1000, cpu_us / repeats, cpu_us / wall);
        std::fflush(stdout);
      }
    }
    if (ok)
      ok = comm->EndOperation(bytes, &error);
    if (!ok)
      break;
  }
  std::printf("%s\n", comm->Describe().c_str());
  if (!ok)
    std::fprintf(stderr, "%s\n", error.c_str());
  // Abort/drain the transport before freeing anything queued GPU work can use.
  comm.reset();
  (void)hipStreamSynchronize(stream);
  (void)hipFree(errors);
  (void)hipFree(scratch);
  (void)hipFree(output);
  (void)hipFree(local);
  (void)hipStreamDestroy(stream);
  if (mismatch) {
    const bool rejected =
        !ok && error.find("scope/ordinal/shape mismatch") != std::string::npos;
    std::printf("expected_collective_mismatch=%u\n", rejected ? 1 : 0);
    return rejected ? 0 : 1;
  }
  return ok ? 0 : 1;
}
