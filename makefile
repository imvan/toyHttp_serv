.PHONY:clean

OBJS = main.o\
		http_conn.o
LIBS = -lhiredis -lpthread

myhttpserv: $(OBJS)
	g++ -o myhttpserv $(OBJS) $(LIBS) `mysql_config --cflags --libs`

clean:
	-rm -rf $(OBJS) myhttpserv
