.PHONY:clean

OBJS = main.o\
		http_conn.o
LIBS = -lhiredis -lpthread

myhttpserv: $(OBJS)
	g++ -o myhttpserv $(OBJS) $(LIBS)

clean:
	-rm -rf $(OBJS) myhttpserv
