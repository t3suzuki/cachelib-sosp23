
N_TH ?= 1

all:
	cd ../build-cachelib/ && ninja && ninja install
	cd _build2 && ninja s3fifo
	./_build2/s3fifo zipf0.4_1M_100M.oracleGeneral.bin 6000 14 $(N_TH)
#	./_build2/s3fifo zipf0.99_1M_100M.oracleGeneral.bin 6000 14 $(N_TH)
#	./_build2/s3fifo zipf0.99_100_1K.oracleGeneral.bin 6000 10 $(N_TH)
#	sudo ~/perf record --call-graph=dwarf -C 0 -g taskset -c 0 ./_build2/s3fifo zipf0.99_1M_100M.oracleGeneral.bin 6000 24 $(N_TH)

flamegraph:
	sudo ~/perf script | perl ~/FlameGraph/stackcollapse-perf.pl | perl ~/FlameGraph/flamegraph.pl > ~/hoge.svg
