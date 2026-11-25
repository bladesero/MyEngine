file(MAKE_DIRECTORY "${fixture}/Content/Scenes")
file(WRITE "${fixture}/MyEngine.project.json"
    [=[{"name":"Malformed","startupScene":"Content/Scenes/Test.scene.json"}]=])
file(WRITE "${fixture}/Content/Scenes/Test.scene.json" [=[
{"actors":[{"id":1,"parentID":2,"name":"First"},{"id":2,"parentID":1,"name":"Second"}]}
]=])
execute_process(
    COMMAND "${editor}" --self-test --project "${fixture}"
    RESULT_VARIABLE result ERROR_VARIABLE diagnostic)
if(NOT result STREQUAL "1" OR NOT diagnostic MATCHES "hierarchy is cyclic or too deep")
    message(FATAL_ERROR "Malformed hierarchy did not fail cleanly: ${result}: ${diagnostic}")
endif()
