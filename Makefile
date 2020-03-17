# TRYHW=""

dbg_guv_server: *.h *.c
	gcc -g ${TRYHW} -Wall -fno-diagnostics-show-caret -o dbg_guv_server *.c -lpthread

clean:
	rm -rf dbg_guv_server
