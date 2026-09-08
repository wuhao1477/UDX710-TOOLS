CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror
CPPFLAGS = -I../src/include/system

.PHONY: test clean notification_test sms_parser_test telemetry_codec_test

test: device_profile_test builtin_card_rules_test goform_json_test wifi_clients_test notification_test sms_parser_test telemetry_codec_test
	./device_profile_test
	./builtin_card_rules_test
	./goform_json_test
	./wifi_clients_test
	./notification_test
	./sms_parser_test
	./telemetry_codec_test

device_profile_test: device_profile_test.c ../src/system/device_profile.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^

builtin_card_rules_test: builtin_card_rules_test.c ../src/system/builtin_card_rules.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^

goform_json_test: goform_json_test.c ../src/system/goform_client.c ../src/system/builtin_card_rules.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^

wifi_clients_test: wifi_clients_test.c ../src/system/wifi_clients.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^

notification_test: notification_test.c telemetry_link_stub.c ../src/system/notification_rules.c ../src/system/exec_utils.c
	$(CC) $(CFLAGS) -Wno-unused-parameter $(CPPFLAGS) -o $@ $^

sms_parser_test: sms_parser_test.c ../src/system/sms_parser.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^

telemetry_codec_test: telemetry_codec_test.c ../src/system/telemetry_codec.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^

clean:
	rm -f device_profile_test builtin_card_rules_test goform_json_test wifi_clients_test notification_test sms_parser_test telemetry_codec_test
