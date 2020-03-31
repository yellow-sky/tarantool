#include <stdint.h>
#include <stddef.h>
#include "json.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  int rc;
  size = 0;
  data = NULL;
  /*
  struct uri *uri = NULL;
  const char *str = (char*)data;
  rc = uri_parse(uri, str);
  if (rc != 0) {
    return rc;
  }
  */

  return 0;
}
