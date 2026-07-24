{
    expected_error = "dependency-cycle",
    manifest = {
        targets = {
            ["Library.A"] = {deps = {"Library.B"}},
            ["Library.B"] = {deps = {"Library.A"}}
        }
    }
}
