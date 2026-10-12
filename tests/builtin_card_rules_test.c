#include <assert.h>
#include <string.h>

#include "builtin_card_rules.h"

static void test_operator_mapping(void) {
  assert(builtin_card_priority_for_operator("46000") == 7);
  assert(builtin_card_priority_for_operator("46001") == 11);
  assert(builtin_card_priority_for_operator("46003") == 9);
  assert(builtin_card_priority_for_operator("46011") == 9);
  assert(builtin_card_priority_for_operator("46015") == -1);
  assert(strcmp(builtin_card_operator_name("46001"), "中国联通") == 0);
}

static void test_profile_gate(void) {
  assert(builtin_card_profile_supported("SZ", "541", "SRB876"));
  assert(builtin_card_profile_supported("SZ", "542", "SRB876"));
  assert(!builtin_card_profile_supported("UDX", "542", "SRB876"));
  assert(!builtin_card_profile_supported("SZ", "543", "SRB876"));
  assert(builtin_card_card_is_switchable("898600", 2));
  assert(!builtin_card_card_is_switchable("898600", 1));
  assert(!builtin_card_card_is_switchable("", 2));
  assert(!builtin_card_card_is_switchable(NULL, 2));
}

static void test_real_name_parser(void) {
  int status = -1;

  assert(builtin_card_parse_real_name_status(
             "{\"code\":0,\"success\":true,\"data\":{"
             "\"realNameStatus\":2}}",
             &status) == 0);
  assert(status == 2);
  assert(builtin_card_parse_real_name_status(
             "{\"code\":0,\"success\":true,\"data\":{"
             "\"realNameStatus\":1}}",
             &status) == 0);
  assert(status == 1);
  assert(builtin_card_parse_real_name_status(
             "{\"code\":0,\"success\":true,\"data\":\"\","
             "\"realNameStatus\":2}",
             &status) != 0);
  assert(builtin_card_parse_real_name_status(
             "{\"code\":500,\"success\":true,\"data\":{"
             "\"realNameStatus\":2}}",
             &status) != 0);
  assert(builtin_card_parse_real_name_status(
             "{\"code\":0,\"success\":false,\"data\":{"
             "\"realNameStatus\":2}}",
             &status) != 0);
  assert(builtin_card_parse_real_name_status(
             "{\"code\":0,\"success\":true,\"data\":{"
             "\"realNameStatus\":\"2\"}}",
             &status) != 0);
}

static void test_priority_query(void) {
  char query[160];

  assert(builtin_card_build_priority_query(11, query, sizeof(query)) == 0);
  assert(strcmp(query,
                "goformId=setDeviceConfig&configOption=setPriorityMnc&"
                "priorityMnc=11&configValue=11&save=1") == 0);
  assert(builtin_card_build_priority_query(8, query, sizeof(query)) != 0);
}

int main(void) {
  test_operator_mapping();
  test_profile_gate();
  test_real_name_parser();
  test_priority_query();
  return 0;
}
