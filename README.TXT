1. Build:
$ make
$ python3 setup.py install --user

2. Allocate shared memory queue
$ ./queue alloc # will create /dev/shm/qwe

3. Run conntrack events generator:
$ ./queue send  # will start events generator in foreground
$ # ./queue recv  # will receive messages. Please do not start it at the same time with Python version.

4. Run Python message receiver:
$ ./test.py

