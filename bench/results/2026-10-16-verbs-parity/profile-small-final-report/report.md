# Transport profile

Diagnostic timings include profiling overhead. GPU durations use a separate clock.
Nested and parallel rows are not additive. No cross-host clock subtraction.

## fuzzy

Records: 14870.

- 6 records with negative error status (retained)
- dma_watchdog: 96 events (retained)

## misty

Records: 14678.

- 9 records with negative error status (retained)
- prepare_retry_sleep: 1 events (retained)

## Phase distributions

Times in µs; counters in their stated units. All samples, including failures, retained.

| Capture | Kind | Phase | Bytes | Dir | Op | N | P50 | P95 | P99 | Max |
|---|---|---|---:|---|---|---:|---:|---:|---:|---:|
| fuzzy | derived | dma_callback_span | 10304 | rx | 4 | 384 | 0.110 | 0.920 | 2.071 | 5.920 |
| fuzzy | derived | dma_callback_span | 10304 | tx | 4 | 384 | 0.120 | 1.160 | 2.142 | 5.250 |
| fuzzy | derived | post_to_first_callback | 10304 | rx | 4 | 384 | 23.934 | 42.815 | 109.611 | 1059143.942 |
| fuzzy | derived | post_to_first_callback | 10304 | tx | 4 | 384 | 8.369 | 24.553 | 47.069 | 65.768 |
| fuzzy | derived | tx_callback_resume | 10304 | tx | 4 | 128 | 2.010 | 4.340 | 17.968 | 20.409 |
| fuzzy | derived | tx_unattributed | 0 | tx | 1 | 130 | 1.500 | 2.619 | 6.940 | 11.110 |
| fuzzy | derived | tx_unattributed | 10304 | tx | 4 | 128 | 2.755 | 3.768 | 6.190 | 9.490 |
| fuzzy | kernel | batch | 0 | tx | 1 | 130 | 23.714 | 37.445 | 156.058 | 497.492 |
| fuzzy | kernel | batch | 10304 | tx | 4 | 128 | 52.728 | 117.830 | 595.207 | 1801.845 |
| fuzzy | kernel | batch_build | 0 | rx | 1 | 130 | 0.610 | 0.708 | 2.270 | 2.530 |
| fuzzy | kernel | batch_build | 0 | tx | 1 | 130 | 0.620 | 0.721 | 1.093 | 2.870 |
| fuzzy | kernel | batch_build | 10304 | rx | 4 | 128 | 1.265 | 2.466 | 237.876 | 540.290 |
| fuzzy | kernel | batch_build | 10304 | tx | 4 | 128 | 1.360 | 2.340 | 201.039 | 564.219 |
| fuzzy | kernel | batch_dma_done | 10304 | rx | 4 | 128 | 0.000 | 0.000 | 0.000 | 0.000 |
| fuzzy | kernel | batch_dma_done | 10304 | tx | 4 | 128 | 0.000 | 0.000 | 0.000 | 0.000 |
| fuzzy | kernel | batch_post | 0 | rx | 1 | 130 | 0.030 | 0.050 | 0.344 | 0.380 |
| fuzzy | kernel | batch_post | 10304 | rx | 4 | 128 | 1.800 | 2.689 | 10.257 | 23.099 |
| fuzzy | kernel | batch_post | 10304 | tx | 4 | 128 | 2.710 | 3.651 | 5.182 | 5.510 |
| fuzzy | kernel | batch_release | 0 | rx | 1 | 130 | 0.060 | 0.185 | 0.524 | 0.550 |
| fuzzy | kernel | batch_release | 0 | tx | 1 | 130 | 0.190 | 0.334 | 0.523 | 0.720 |
| fuzzy | kernel | batch_release | 10304 | rx | 4 | 128 | 0.940 | 1.576 | 3.672 | 4.179 |
| fuzzy | kernel | batch_release | 10304 | tx | 4 | 128 | 0.890 | 1.033 | 1.299 | 1.480 |
| fuzzy | kernel | control_dispatch | 0 | rx | 1 | 130 | 0.360 | 0.475 | 2.251 | 2.680 |
| fuzzy | kernel | control_dispatch | 10304 | rx | 4 | 129 | 0.370 | 0.530 | 3.393 | 4.359 |
| fuzzy | kernel | control_exchange | 0 | tx | 1 | 130 | 21.664 | 34.803 | 148.702 | 483.602 |
| fuzzy | kernel | control_exchange | 10304 | tx | 4 | 128 | 23.059 | 33.922 | 303.696 | 657.737 |
| fuzzy | kernel | control_response | 0 | rx | 1 | 130 | 0.870 | 1.526 | 4.439 | 4.630 |
| fuzzy | kernel | control_response | 10304 | rx | 4 | 129 | 0.960 | 1.940 | 4.568 | 5.040 |
| fuzzy | kernel | control_rx_lock | 0 | rx | 1 | 130 | 0.150 | 0.180 | 0.207 | 0.250 |
| fuzzy | kernel | control_rx_lock | 10304 | rx | 4 | 129 | 0.150 | 0.186 | 0.210 | 0.220 |
| fuzzy | kernel | control_service | 0 | rx | 1 | 130 | 3.615 | 5.516 | 19.059 | 19.919 |
| fuzzy | kernel | control_service | 10304 | rx | 4 | 129 | 4.940 | 6.835 | 247.570 | 576.799 |
| fuzzy | kernel | cq_publish | 0 | rx | 128 | 130 | 0.000 | 0.000 | 0.000 | 0.000 |
| fuzzy | kernel | cq_publish | 0 | tx | 0 | 130 | 0.000 | 0.000 | 0.000 | 0.000 |
| fuzzy | kernel | cq_publish | 0 | tx | 1 | 128 | 0.000 | 0.000 | 0.000 | 0.000 |
| fuzzy | kernel | cq_publish | 10304 | rx | 129 | 128 | 0.000 | 0.000 | 0.000 | 0.000 |
| fuzzy | kernel | cq_push | 0 | rx | 128 | 130 | 0.250 | 0.500 | 1.060 | 1.110 |
| fuzzy | kernel | cq_push | 0 | tx | 0 | 130 | 0.140 | 0.308 | 0.473 | 0.820 |
| fuzzy | kernel | cq_push | 0 | tx | 1 | 128 | 0.140 | 0.267 | 0.380 | 0.500 |
| fuzzy | kernel | cq_push | 10304 | rx | 129 | 128 | 0.260 | 0.738 | 1.078 | 1.420 |
| fuzzy | kernel | dma_first | 10304 | rx | 4 | 384 | 0.000 | 0.000 | 0.000 | 0.000 |
| fuzzy | kernel | dma_first | 10304 | tx | 4 | 384 | 0.000 | 0.000 | 0.000 | 0.000 |
| fuzzy | kernel | dma_last | 10304 | rx | 4 | 384 | 0.000 | 0.000 | 0.000 | 0.000 |
| fuzzy | kernel | dma_last | 10304 | tx | 4 | 384 | 0.000 | 0.000 | 0.000 | 0.000 |
| fuzzy | kernel | dma_watchdog | 10304 | rx | 4 | 96 | 0.000 | 0.000 | 0.000 | 0.000 |
| fuzzy | kernel | done_received | 10304 | tx | 4 | 128 | 0.000 | 0.000 | 0.000 | 0.000 |
| fuzzy | kernel | done_send | 0 | rx | 1 | 130 | 0.050 | 0.095 | 0.301 | 0.340 |
| fuzzy | kernel | done_send | 10304 | rx | 4 | 128 | 1.575 | 4.299 | 15.284 | 23.529 |
| fuzzy | kernel | done_submit | 10304 | rx | 4 | 128 | 1.445 | 4.002 | 14.540 | 22.779 |
| fuzzy | kernel | done_wait | 10304 | tx | 4 | 128 | 12.730 | 44.097 | 75.585 | 480.093 |
| fuzzy | kernel | lane_alloc | 10304 | rx | 4 | 384 | 0.030 | 0.250 | 0.750 | 1.629 |
| fuzzy | kernel | lane_alloc | 10304 | tx | 4 | 384 | 0.030 | 0.200 | 0.417 | 1.049 |
| fuzzy | kernel | lane_build | 10304 | rx | 4 | 384 | 0.040 | 0.120 | 27.639 | 268.921 |
| fuzzy | kernel | lane_build | 10304 | tx | 4 | 384 | 0.040 | 0.150 | 21.832 | 284.200 |
| fuzzy | kernel | lane_post | 10304 | rx | 4 | 512 | 0.150 | 0.290 | 0.607 | 1.260 |
| fuzzy | kernel | lane_post | 10304 | tx | 4 | 512 | 0.600 | 0.764 | 0.997 | 3.520 |
| fuzzy | kernel | mr_attach_index | 100663296 | rx | 0 | 8 | 212.957 | 279.147 | 280.941 | 281.390 |
| fuzzy | kernel | rx_callback_worker | 10304 | rx | 4 | 128 | 1.545 | 5.911 | 11.180 | 16.040 |
| fuzzy | kernel | rx_finish | 0 | rx | 1 | 130 | 0.740 | 1.315 | 2.604 | 2.950 |
| fuzzy | kernel | rx_finish | 10304 | rx | 4 | 128 | 0.990 | 2.099 | 3.687 | 4.170 |
| fuzzy | kernel | rx_paths | 0 | rx | 1 | 130 | 0.514 | 0.983 | 2.261 | 3.450 |
| fuzzy | kernel | rx_paths | 10304 | rx | 4 | 128 | 0.450 | 0.790 | 2.195 | 2.610 |
| fuzzy | kernel | rx_reap | 0 | rx | 1 | 130 | 1.185 | 2.111 | 5.269 | 5.570 |
| fuzzy | kernel | rx_reap | 10304 | rx | 4 | 224 | 3.655 | 7.275 | 19.942 | 34.368 |
| fuzzy | kernel | rx_verbs_begin | 0 | rx | 1 | 130 | 0.420 | 0.973 | 4.107 | 4.739 |
| fuzzy | kernel | rx_verbs_begin | 10304 | rx | 4 | 128 | 0.520 | 1.013 | 3.644 | 5.630 |
| fuzzy | kernel | rx_worker_lock | 10304 | rx | 4 | 128 | 0.150 | 0.277 | 0.393 | 0.460 |
| fuzzy | kernel | tx_dma_wait | 10304 | tx | 4 | 128 | 9.655 | 31.204 | 72.414 | 85.627 |
| fuzzy | kernel | tx_finish | 0 | tx | 1 | 130 | 0.590 | 1.117 | 2.543 | 21.819 |
| fuzzy | kernel | tx_finish | 10304 | tx | 4 | 128 | 0.680 | 0.992 | 1.764 | 3.390 |
| fuzzy | kernel | tx_job | 0 | tx | 1 | 130 | 24.109 | 37.951 | 157.311 | 499.942 |
| fuzzy | kernel | tx_job | 10304 | tx | 4 | 128 | 53.113 | 118.446 | 595.729 | 1802.725 |
| fuzzy | kernel | tx_paths_lock | 0 | tx | 1 | 130 | 0.330 | 0.601 | 1.284 | 2.460 |
| fuzzy | kernel | tx_paths_lock | 10304 | tx | 4 | 128 | 0.230 | 0.513 | 0.665 | 0.680 |
| fuzzy | kernel | tx_queue | 0 | tx | 1 | 130 | 1.185 | 2.466 | 17.884 | 36.919 |
| fuzzy | kernel | tx_queue | 10304 | tx | 4 | 128 | 1.105 | 2.300 | 4.553 | 13.039 |
| fuzzy | kernel | tx_wr | 0 | tx | 1 | 130 | 26.769 | 41.290 | 178.143 | 559.500 |
| fuzzy | kernel | tx_wr | 10304 | tx | 4 | 128 | 55.343 | 123.174 | 599.719 | 1811.625 |
| fuzzy | kernel | verbs_post_recv | 0 | rx | 0 | 266 | 0.275 | 0.355 | 0.803 | 3.890 |
| fuzzy | kernel | verbs_post_send | 0 | tx | 2 | 130 | 1.180 | 1.722 | 4.306 | 7.410 |
| fuzzy | kernel | verbs_post_send | 1 | tx | 2 | 2 | 0.250 | 0.358 | 0.368 | 0.370 |
| fuzzy | kernel | verbs_post_send | 4 | tx | 2 | 2 | 2.845 | 5.082 | 5.280 | 5.330 |
| fuzzy | kernel | verbs_post_send | 32 | tx | 2 | 2 | 0.380 | 0.560 | 0.576 | 0.580 |
| fuzzy | kernel | verbs_post_send | 10304 | tx | 1 | 128 | 0.940 | 1.710 | 2.910 | 4.620 |
| fuzzy | kernel | xdomain_lock | 0 | tx | 1 | 130 | 0.140 | 0.276 | 0.333 | 0.510 |
| fuzzy | kernel | xdomain_lock | 10304 | tx | 4 | 128 | 0.060 | 0.160 | 0.247 | 0.300 |
| fuzzy | kernel | xdomain_request | 0 | tx | 1 | 130 | 20.965 | 33.959 | 146.077 | 479.563 |
| fuzzy | kernel | xdomain_request | 10304 | tx | 4 | 128 | 22.220 | 32.933 | 302.515 | 655.017 |
| misty | derived | dma_callback_span | 10304 | rx | 4 | 384 | 0.100 | 2.381 | 4.827 | 5.530 |
| misty | derived | dma_callback_span | 10304 | tx | 4 | 384 | 0.120 | 2.534 | 5.705 | 17.029 |
| misty | derived | post_to_first_callback | 10304 | rx | 4 | 384 | 22.824 | 54.747 | 170.615 | 546.619 |
| misty | derived | post_to_first_callback | 10304 | tx | 4 | 384 | 8.700 | 37.915 | 363.501 | 479.231 |
| misty | derived | tx_callback_resume | 10304 | tx | 4 | 128 | 1.960 | 4.106 | 18.201 | 19.799 |
| misty | derived | tx_unattributed | 0 | tx | 1 | 130 | 1.435 | 1.942 | 3.687 | 6.160 |
| misty | derived | tx_unattributed | 10304 | tx | 4 | 128 | 2.045 | 2.840 | 5.904 | 6.740 |
| misty | kernel | batch | 0 | tx | 1 | 130 | 22.244 | 37.812 | 109.519 | 386.135 |
| misty | kernel | batch | 10304 | tx | 4 | 128 | 51.838 | 87.189 | 1442.535 | 1059468.164 |
| misty | kernel | batch_build | 0 | rx | 1 | 130 | 0.700 | 1.087 | 1.417 | 2.220 |
| misty | kernel | batch_build | 0 | tx | 1 | 130 | 0.790 | 0.860 | 1.117 | 2.070 |
| misty | kernel | batch_build | 10304 | rx | 4 | 128 | 1.325 | 2.536 | 201.807 | 545.039 |
| misty | kernel | batch_build | 10304 | tx | 4 | 128 | 1.370 | 1.466 | 203.580 | 288.528 |
| misty | kernel | batch_dma_done | 10304 | rx | 4 | 128 | 0.000 | 0.000 | 0.000 | 0.000 |
| misty | kernel | batch_dma_done | 10304 | tx | 4 | 128 | 0.000 | 0.000 | 0.000 | 0.000 |
| misty | kernel | batch_post | 0 | rx | 1 | 130 | 0.030 | 0.050 | 0.179 | 0.330 |
| misty | kernel | batch_post | 10304 | rx | 4 | 128 | 2.110 | 2.926 | 5.451 | 22.430 |
| misty | kernel | batch_post | 10304 | tx | 4 | 128 | 2.575 | 3.336 | 5.462 | 7.259 |
| misty | kernel | batch_release | 0 | rx | 1 | 130 | 0.060 | 0.120 | 0.157 | 0.510 |
| misty | kernel | batch_release | 0 | tx | 1 | 130 | 0.060 | 0.090 | 0.207 | 0.230 |
| misty | kernel | batch_release | 10304 | rx | 4 | 128 | 0.670 | 2.727 | 3.681 | 5.080 |
| misty | kernel | batch_release | 10304 | tx | 4 | 128 | 0.890 | 1.110 | 1.625 | 1.710 |
| misty | kernel | control_dispatch | 0 | rx | 1 | 130 | 0.150 | 0.368 | 0.781 | 2.899 |
| misty | kernel | control_dispatch | 10304 | rx | 4 | 128 | 0.165 | 0.267 | 0.600 | 0.860 |
| misty | kernel | control_exchange | 0 | tx | 1 | 130 | 20.539 | 36.242 | 105.650 | 378.995 |
| misty | kernel | control_exchange | 10304 | tx | 4 | 129 | 24.459 | 35.264 | 799.563 | 1056585.757 |
| misty | kernel | control_response | 0 | rx | 1 | 130 | 0.790 | 1.347 | 1.731 | 13.589 |
| misty | kernel | control_response | 10304 | rx | 4 | 128 | 0.730 | 1.173 | 1.378 | 1.709 |
| misty | kernel | control_rx_lock | 0 | rx | 1 | 130 | 0.040 | 0.250 | 0.250 | 0.680 |
| misty | kernel | control_rx_lock | 10304 | rx | 4 | 128 | 0.040 | 0.060 | 0.070 | 0.700 |
| misty | kernel | control_service | 0 | rx | 1 | 130 | 3.120 | 4.836 | 6.845 | 17.670 |
| misty | kernel | control_service | 10304 | rx | 4 | 128 | 4.740 | 7.079 | 208.189 | 571.698 |
| misty | kernel | cq_publish | 0 | rx | 128 | 130 | 0.000 | 0.000 | 0.000 | 0.000 |
| misty | kernel | cq_publish | 0 | tx | 0 | 130 | 0.000 | 0.000 | 0.000 | 0.000 |
| misty | kernel | cq_publish | 0 | tx | 1 | 128 | 0.000 | 0.000 | 0.000 | 0.000 |
| misty | kernel | cq_publish | 10304 | rx | 129 | 128 | 0.000 | 0.000 | 0.000 | 0.000 |
| misty | kernel | cq_push | 0 | rx | 128 | 130 | 0.150 | 0.270 | 0.558 | 1.470 |
| misty | kernel | cq_push | 0 | tx | 0 | 130 | 0.150 | 0.221 | 0.353 | 0.580 |
| misty | kernel | cq_push | 0 | tx | 1 | 128 | 0.150 | 0.217 | 0.750 | 0.860 |
| misty | kernel | cq_push | 10304 | rx | 129 | 128 | 0.250 | 1.566 | 1.883 | 2.340 |
| misty | kernel | dma_first | 10304 | rx | 4 | 384 | 0.000 | 0.000 | 0.000 | 0.000 |
| misty | kernel | dma_first | 10304 | tx | 4 | 384 | 0.000 | 0.000 | 0.000 | 0.000 |
| misty | kernel | dma_last | 10304 | rx | 4 | 384 | 0.000 | 0.000 | 0.000 | 0.000 |
| misty | kernel | dma_last | 10304 | tx | 4 | 384 | 0.000 | 0.000 | 0.000 | 0.000 |
| misty | kernel | done_received | 10304 | tx | 4 | 128 | 0.000 | 0.000 | 0.000 | 0.000 |
| misty | kernel | done_send | 0 | rx | 1 | 130 | 0.050 | 0.100 | 0.110 | 0.350 |
| misty | kernel | done_send | 10304 | rx | 4 | 128 | 1.995 | 8.691 | 9.606 | 15.429 |
| misty | kernel | done_submit | 10304 | rx | 4 | 128 | 1.800 | 8.042 | 9.005 | 14.659 |
| misty | kernel | done_wait | 10304 | tx | 4 | 128 | 10.475 | 13.738 | 27.893 | 30.319 |
| misty | kernel | lane_alloc | 10304 | rx | 4 | 384 | 0.030 | 0.250 | 0.829 | 7.529 |
| misty | kernel | lane_alloc | 10304 | tx | 4 | 384 | 0.030 | 0.050 | 0.185 | 1.010 |
| misty | kernel | lane_build | 10304 | rx | 4 | 384 | 0.040 | 0.170 | 21.749 | 271.520 |
| misty | kernel | lane_build | 10304 | tx | 4 | 384 | 0.040 | 0.119 | 22.401 | 149.034 |
| misty | kernel | lane_post | 10304 | rx | 4 | 512 | 0.170 | 0.274 | 0.804 | 1.460 |
| misty | kernel | lane_post | 10304 | tx | 4 | 512 | 0.570 | 0.754 | 0.899 | 3.900 |
| misty | kernel | mr_attach_index | 100663296 | rx | 0 | 8 | 145.379 | 270.217 | 270.883 | 271.050 |
| misty | kernel | prepare_retry_sleep | 10304 | tx | 4 | 1 | 2015.091 | 2015.091 | 2015.091 | 2015.091 |
| misty | kernel | rx_callback_worker | 10304 | rx | 4 | 128 | 1.525 | 9.918 | 11.659 | 12.629 |
| misty | kernel | rx_finish | 0 | rx | 1 | 130 | 0.480 | 0.800 | 1.216 | 2.900 |
| misty | kernel | rx_finish | 10304 | rx | 4 | 128 | 1.100 | 3.878 | 4.237 | 4.600 |
| misty | kernel | rx_paths | 0 | rx | 1 | 130 | 0.380 | 0.580 | 0.968 | 2.710 |
| misty | kernel | rx_paths | 10304 | rx | 4 | 128 | 0.210 | 0.396 | 0.875 | 1.010 |
| misty | kernel | rx_reap | 0 | rx | 1 | 130 | 0.890 | 1.591 | 2.212 | 5.280 |
| misty | kernel | rx_reap | 10304 | rx | 4 | 128 | 4.205 | 16.886 | 20.287 | 25.579 |
| misty | kernel | rx_verbs_begin | 0 | rx | 1 | 130 | 0.410 | 0.876 | 1.171 | 3.090 |
| misty | kernel | rx_verbs_begin | 10304 | rx | 4 | 128 | 0.410 | 1.067 | 1.462 | 2.370 |
| misty | kernel | rx_worker_lock | 10304 | rx | 4 | 128 | 0.160 | 0.280 | 0.377 | 0.420 |
| misty | kernel | tx_dma_wait | 10304 | tx | 4 | 128 | 9.855 | 45.428 | 308.981 | 484.221 |
| misty | kernel | tx_finish | 0 | tx | 1 | 130 | 0.570 | 0.872 | 1.345 | 2.340 |
| misty | kernel | tx_finish | 10304 | tx | 4 | 128 | 0.590 | 1.016 | 1.710 | 2.110 |
| misty | kernel | tx_job | 0 | tx | 1 | 130 | 22.579 | 38.067 | 110.348 | 387.885 |
| misty | kernel | tx_job | 10304 | tx | 4 | 128 | 52.068 | 87.423 | 1443.105 | 1059468.654 |
| misty | kernel | tx_paths_lock | 0 | tx | 1 | 130 | 0.125 | 0.470 | 0.774 | 1.810 |
| misty | kernel | tx_paths_lock | 10304 | tx | 4 | 128 | 0.110 | 0.177 | 0.272 | 0.750 |
| misty | kernel | tx_queue | 0 | tx | 1 | 130 | 1.365 | 2.876 | 15.963 | 16.250 |
| misty | kernel | tx_queue | 10304 | tx | 4 | 128 | 1.045 | 2.405 | 3.511 | 5.860 |
| misty | kernel | tx_wr | 0 | tx | 1 | 130 | 25.224 | 42.354 | 127.715 | 406.484 |
| misty | kernel | tx_wr | 10304 | tx | 4 | 128 | 54.303 | 89.496 | 1449.886 | 1059471.854 |
| misty | kernel | verbs_post_recv | 0 | rx | 0 | 266 | 0.180 | 0.320 | 0.384 | 2.290 |
| misty | kernel | verbs_post_send | 0 | tx | 2 | 130 | 1.034 | 1.541 | 1.886 | 8.260 |
| misty | kernel | verbs_post_send | 1 | tx | 2 | 2 | 0.155 | 0.177 | 0.179 | 0.180 |
| misty | kernel | verbs_post_send | 4 | tx | 2 | 2 | 2.635 | 4.638 | 4.816 | 4.860 |
| misty | kernel | verbs_post_send | 32 | tx | 2 | 2 | 0.210 | 0.228 | 0.230 | 0.230 |
| misty | kernel | verbs_post_send | 10304 | tx | 1 | 128 | 0.650 | 1.230 | 2.392 | 2.730 |
| misty | kernel | xdomain_lock | 0 | tx | 1 | 130 | 0.190 | 0.345 | 0.367 | 0.430 |
| misty | kernel | xdomain_lock | 10304 | tx | 4 | 129 | 0.030 | 0.060 | 0.060 | 0.070 |
| misty | kernel | xdomain_request | 0 | tx | 1 | 130 | 20.019 | 35.791 | 104.119 | 376.035 |
| misty | kernel | xdomain_request | 10304 | tx | 4 | 129 | 24.049 | 34.537 | 796.584 | 1056584.237 |
