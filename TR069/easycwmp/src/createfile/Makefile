FILES=./createfile.c
OBJS=$(patsubst %.c, %.o, $(FILES))

all: createfile

createfile:$(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(OBJS):%.o:%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm *.o createfile
