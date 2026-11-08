Follow-up runs for 20261108_021518_full, made 2026-11-08 02:21 on max-Lenovo-Legion-R9000X2021R by scripts/followup_amd.sh. bench_topk reported avx2=1.
Every command is in commands.log.
bandwidth_all_threads.csv: pure stream read of 512M at 1,2,3,4,6,8,12,16 threads.
merge_path_k1024_vs_k1023.csv: K = 1023 is not a multiple of 8, so it takes the scalar merge path with the same filter.
l2_prefetch_events.csv: l2_pf_miss_l2_hit_l3,l2_pf_miss_l2_l3 over the 20 timed reps of the tiled engine, K = 64, one thread. These replace LLC-load-misses, which this CPU does not support. cache_lines = N * 4 / 64 * reps.
l3_by_algorithm.csv: the same perf region, N = 32M, 10 reps, adding ls_dmnd_fills_from_sys.mem_io_local so demand fills from memory count too. memory_fetches_per_line = (l2_pf_miss_l2_l3 + demand fills) / cache_lines; reads only.
