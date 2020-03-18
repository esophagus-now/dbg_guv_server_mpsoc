DBG=

dbg_guv_server: *.h *.c
	gcc -g ${DBG} -Wall -fno-diagnostics-show-caret -o dbg_guv_server *.c -lpthread

clean:
	rm -rf dbg_guv_server
