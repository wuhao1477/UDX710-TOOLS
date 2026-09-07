CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror
CPPFLAGS = -I../src/include/system

.PHONY: test clean

test: device_profile_test builtin_card_rules_test goform_json_test
	./device_profile_test
	./builtin_card_rules_test
	./goform_json_test

device_profile_test: device_profile_test.c ../src/system/device_profile.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^

builtin_card_rules_test: builtin_card_rules_test.c ../src/system/builtin_card_rules.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^

goform_json_test: goform_json_test.c ../src/system/goform_client.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^

clean:
	rm -f device_profile_test builtin_card_rules_test goform_json_test
