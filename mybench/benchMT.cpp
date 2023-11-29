

#include <assert.h>
#include <errno.h>
#include <glog/logging.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <sys/time.h>
#include <sysexits.h>
#include <unistd.h>

#include <atomic>
#include <thread>
#include <vector>

#include "cache.h"
#include "bench.h"
#include "reader.h"
#include "request.h"
#include "zipf.h"

using namespace std;

static atomic<bool> STOP_FLAG = true;

// struct thread_args {
//   struct bench_data *bench_data;
//   bench_opts_t *opts;
//   int thread_id;
// };

struct thread_res {
  int64_t n_get;
  int64_t n_set;
  int64_t n_get_miss;
  int64_t n_del;

  int64_t trace_time;
};

static void pin_thread_to_core(int core_id) {
#if !defined(__APPLE__)
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id * 2, &cpuset);
  pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

  LOG(INFO) << "pin thread " << pthread_self() << " to core " << core_id;
#endif
}


#define MAX_CORE (8)
#define THETA (0.3)
#define N_ITEM (1024ULL*1024*1024*16)
#define ALIGN_SIZE (64)
class Genr {
private:
  struct zipf * zipf;
public:
  Genr(int seed) {
    double theta = THETA;
    char *zipf_theta_str = getenv("ZIPF_THETA");
    if (zipf_theta_str) {
      theta = atof(zipf_theta_str);
    }
    printf("Zipf theta = %f\n", theta);
    zipf = zipf_create(N_ITEM, theta, seed);
  }
  inline uint64_t gen() {
    return zipf_generate(zipf);
  }
};

static Genr *genr[MAX_CORE];

static inline std::string gen_strkey(uint64_t key) {
  auto strkey = fmt::format_int(key).str();
  return strkey;
}

static inline int cache_lookup(Cache *cache, uint64_t key, uint64_t &value) {
  Cache::ReadHandle item_handle = cache->find(gen_strkey(key));
  if (item_handle) {
    assert(item_handle->getSize() == ALIGN_SIZE);
    const char *data = reinterpret_cast<const char *>(item_handle->getMemory());
    return 1; // hit
  } else {
    return 0; // miss
  }
}

static inline bool cache_miss(struct bench_data *bdata, uint64_t key, uint64_t value) {
  Cache::WriteHandle item_handle = bdata->cache->allocate(bdata->pool, gen_strkey(key), ALIGN_SIZE);
  if (item_handle == nullptr || item_handle->getMemory() == nullptr) {
    return 1;
  }
  std::memcpy(item_handle->getMemory(), &value, sizeof(uint64_t));
  bdata->cache->insertOrReplace(item_handle);
  return 0;
}


static void trace_replay_run_thread(struct bench_data *bdata,
                                    bench_opts_t *opts, int thread_id,
                                    struct thread_res *res) {
  pin_thread_to_core(thread_id - 1);
  // pthread_setname_np(pthread_self(), "trace_replay_" + to_string(thread_id));

  struct request *req = new_request();
  struct reader *reader =
      open_trace(opts->trace_path, opts->trace_type, thread_id);

  res->n_get = res->n_set = res->n_get_miss = res->n_del = 0;

  int status = read_trace(reader, req);
  assert(status == 0);
  LOG(INFO) << "thread " << thread_id << " read " << *(uint64_t *)req->key
            << ", wait to start";

  while (STOP_FLAG.load()) {
    // wait for all threads to be ready
    ;
  }

  LOG(INFO) << "thread " << thread_id << " start";
#if 0
  while (read_trace(reader, req) == 0) {
    if (res->n_get % 1000 == 0 && thread_id == 1) {
        util::setCurrentTimeSec(req->timestamp);
    }
    status = cache_go(bdata->cache, bdata->pool, req, &res->n_get, &res->n_set,
                      &res->n_del, &res->n_get_miss);

    if (res->n_get % 1000000 == 0) {
      if (STOP_FLAG.load()) {
        break;
      }
      res->trace_time = req->timestamp;
    }
  }
#else
  int t = 0;
  while (1) {
    //uint64_t k = genr[thread_id - 1]->gen();
    uint64_t k = t++;
    uint64_t v;
    
    if (res->n_get % 1000 == 0 && thread_id == 1) {
        util::setCurrentTimeSec(req->timestamp);
    }
    
    res->n_get++;
    int hit = cache_lookup(bdata->cache, k, v);
    if (hit == 0) {
      res->n_get_miss++;
      cache_miss(bdata, k, k);
    }

    if (res->n_get % 1000000 == 0) {
      if (STOP_FLAG.load()) {
        break;
      }
    }
  }
#endif
  res->trace_time = req->timestamp;
  STOP_FLAG.store(true);
  LOG(INFO) << "thread " << thread_id << " finishes";
}

static void aggregate_results(struct bench_data *bdata, bench_opts_t *opts,
                              struct thread_res *res) {
  int n_thread = opts->n_thread;

  bdata->n_get = bdata->n_set = bdata->n_get_miss = bdata->n_del = 0;
  bdata->trace_time = 0;

  int64_t min_trace_time = INT64_MAX, max_trace_time = INT64_MIN;

  for (int i = 0; i < n_thread; i++) {
    bdata->n_get += res[i].n_get;
    bdata->n_set += res[i].n_set;
    bdata->n_get_miss += res[i].n_get_miss;
    bdata->n_del += res[i].n_del;

    if (res[i].trace_time < min_trace_time) {
      min_trace_time = res[i].trace_time;
    }
    if (res[i].trace_time > max_trace_time) {
      max_trace_time = res[i].trace_time;
    }
  }
  bdata->trace_time = max_trace_time;
  util::setCurrentTimeSec(min_trace_time);
  // printf("min trace time: %ld, max trace time: %ld\n", min_trace_time,
  //        max_trace_time);
}

void trace_replay_run_mt(struct bench_data *bdata, bench_opts_t *opts) {
  int n_thread = opts->n_thread;
  struct thread_res *res = new struct thread_res[n_thread];

  std::vector<std::thread> threads;
  for (int i = 0; i < n_thread; i++) {
    genr[i] = new Genr(i);
    threads.push_back(
        std::thread(trace_replay_run_thread, bdata, opts, i + 1, &res[i]));
  }

  // wait for all threads to be ready
  sleep(2);
  STOP_FLAG.store(false);
  gettimeofday(&bdata->start_time, NULL);

  // we wait for one thread to finish, then stop all threads
  while (!STOP_FLAG.load()) {
    sleep(8);
    aggregate_results(bdata, opts, res);
    report_bench_result(bdata, opts);
  }

  // wait for all threads to finish
  for (int i = 0; i < n_thread; i++) {
    threads[i].join();
  }

  gettimeofday(&bdata->end_time, nullptr);

  aggregate_results(bdata, opts, res);

  delete[] res;
}
