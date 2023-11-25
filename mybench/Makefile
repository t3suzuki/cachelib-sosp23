all:
	cd ../build-cachelib/ && ninja && ninja install
	cd _build && ninja
	./_build/s3fifo zipf0.4_1M_10M.oracleGeneral.bin 6000 12 1
