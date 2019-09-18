## KVSSD Storage Engine Module for MongoDB



Execute this series of commands to compile MongoDB with KVSSD storage engine:

    # install compression libraries (zlib, bzip2, snappy):
    sudo apt-get install zlib1g-dev; sudo apt-get install libbz2-dev; sudo apt-get install libsnappy-dev
    # get kvssd library
    git clone https://github.com/shannon-sys/kv_libcpp.git
    # compile kvssd library
    make && make install

Start `mongod` using the `--storageEngine=rocksdb` option.
