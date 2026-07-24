#include "TestHarness.h"
#include "RuntimeModule/RuntimeModule.h"

int main(int argc, char** argv) {
    InitializeMyEngineRuntimeModules();
    return RunRegisteredTests(argc, argv);
}
