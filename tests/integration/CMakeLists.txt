add_test(
  NAME integration.native_process_smoke
  COMMAND
    "${PROJECT_SOURCE_DIR}/scripts/assert-native-process-smoke"
    --executable "$<TARGET_FILE:blob-royale>"
    --config "${PROJECT_SOURCE_DIR}/config/blob-royale.cfg"
    --scenario "${PROJECT_SOURCE_DIR}/tests/fixtures/player-on-wall-collision-test.csv"
    --address "127.0.0.1"
    --port "18081"
)

set_tests_properties(integration.native_process_smoke PROPERTIES
  LABELS "integration;process"
  RESOURCE_LOCK "blob_royale_loopback_port_18081"
  RUN_SERIAL TRUE
  TIMEOUT 30
)
