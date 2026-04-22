.PHONY: all clean

all:
	gcc -o futex_v1 futex_v1_spinlock.c -pthread
	gcc -o futex_v2 futex_v2_yield_spinlock.c -pthread
	gcc -o futex_v3 futex_v3_futex_lock.c -pthread
	gcc -o futex_v4 futex_v4_final.c -pthread

clean:
	rm -f futex_v1 futex_v2 futex_v3 futex_v4
