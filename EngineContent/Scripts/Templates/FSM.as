class FSM {
  string state = "idle";

  void SetState(const string &in next) {
    if (state == next) return;
    Events::Emit("fsm.exit." + state, "{}");
    state = next;
    Events::Emit("fsm.enter." + state, "{}");
  }

  bool IsState(const string &in value) {
    return state == value;
  }
}
