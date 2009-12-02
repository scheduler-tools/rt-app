all:
	gcc -O2 -o rt-app -lqreslib rt-app.c  -lrt -laquosa-qosmgr -laquosa-modules -lqos_util -lqmgr_util

clean:
	rm -f ./rt-app
