CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror
CPPFLAGS = -I../src/include/system

.PHONY: test clean builtin_card_rules_test goform_json_test

test: builtin_card_rules_test goform_json_test
	./builtin_card_rules_test
	./goform_json_test

builtin_card_rules_test: builtin_card_rules_test.c ../src/system/builtin_card_rules.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^

goform_json_test: goform_json_test.c ../src/system/goform_client.c ../src/system/builtin_card_rules.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^

clean:
	rm -f builtin_card_rules_test goform_json_test
