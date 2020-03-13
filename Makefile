dbg_guv_server: *.h *.c
	gcc -g -Wall -fno-diagnostics-show-caret -o dbg_guv_server *.c -lpthread
