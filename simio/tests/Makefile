TESTS = test_timer

UTIL_OBJS=btree.o chipinfo.o ctrlc.o dis.o expr.o list.o opdb.o output.o stab.o util.o vector.o
DRIVERS_OBJS=device.o

CFLAGS=-ggdb -I../../simio -I../../drivers -I../../util
LIBS=-lpthread

OBJS+=$(foreach obj, $(UTIL_OBJS), ../../util/$(obj))
OBJS+=$(foreach obj, $(DRIVERS_OBJS), ../../drivers/$(obj))

test: $(TESTS)
	@for test in $(TESTS); do echo "==== $${test} ===="; ./$${test}; done

test_timer.o : ../simio_timer.c

define add-obj-rule
$(1): $(1:.o=.c)
	$$(CC) $$(CFLAGS) -c $$< -o $$@
endef
$(foreach obj, $(OBJS), $(eval $(call add-obj-rule, $(obj))))

define add-test-rule
$(1): $(1).o $(OBJS)
	$$(CC) -o $$@ $$< $(OBJS) $(LIBS)
endef
$(foreach test, $(TESTS), $(eval $(call add-test-rule, $(test))))

clean:
	-rm -f $(TESTS:=.o) $(TESTS)
