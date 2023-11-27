
#include <glog/logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>

#include "bench.h"
#include "cache.h"
#include "cmd.h"
#include "reader.h"
#include "request.h"

int main(int argc, char *argv[]) {
  google::InitGoogleLogging("mybench");

  bench_opts_t opts = parse_cmd(argc, argv);
  struct bench_data bench_data;
  memset(&bench_data, 0, sizeof(bench_data));

  mycache_init(opts.cache_size_in_mb, opts.hashpower, &bench_data.cache,
               &bench_data.pool);

#if 1 // tomoya-s
#define VAL_LEN (16)
  // warmup
  int64_t i;
  int i_th;
  int n_th = opts.n_thread;
  uint64_t cnt = 0;
  printf("warmup...\n");
  for (i=(1<<(opts.hashpower-1))/n_th; i>=0; i--) {
    for (i_th=0; i_th<n_th; i_th++) {
      //printf("warmup... %d\n", i);
      struct request req;
      char dummy_val[VAL_LEN];
      req.timestamp = 86400 * -2;
      if (n_th > 1) {
	*(uint64_t *)req.key = (uint64_t)i + (i_th+1) * 1000000000ULL;
      } else {
	*(uint64_t *)req.key = (uint64_t)i;
      }
      req.key_len = 8;
      req.val = dummy_val;
      req.val_len = VAL_LEN;
      req.op = op_set;
      req.ttl = 200000000;
      struct bench_data *bdata = &bench_data;
      cache_go(bdata->cache, bdata->pool, &req, &bdata->n_get, &bdata->n_set,
	       &bdata->n_del, &bdata->n_get_miss);
      cnt++;
    }
  }
  printf("warmup done! %ld items inserted\n", cnt);
#endif

  if (opts.n_thread == 1) {
    trace_replay_run(&bench_data, &opts);
  } else {
    trace_replay_run_mt(&bench_data, &opts);
  }

  report_bench_result(&bench_data, &opts);

  return 0;
}
