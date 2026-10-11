CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror
CPPFLAGS = -I../src/include/system

.PHONY: test clean builtin_card_rules_test goform_json_test goform_async_test

test: builtin_card_rules_test goform_json_test goform_async_test
	./builtin_card_rules_test
	./goform_json_test
	./goform_async_test

builtin_card_rules_test: builtin_card_rules_test.c ../src/system/builtin_card_rules.c ../src/system/strict_json.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^

goform_json_test: goform_json_test.c ../src/system/goform_client.c ../src/system/builtin_card_rules.c ../src/system/strict_json.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^

goform_async_test: goform_async_test.c ../src/system/goform_async.c ../src/mongoose.c
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -I../src -I../src/include/system -pthread -o $@ $^

clean:
	rm -f builtin_card_rules_test goform_json_test goform_async_test
