#include "Event.h"

void EventQueue::Push(const Event& event) {
    m_Queue.push(event);
}

bool EventQueue::Empty() const {
    return m_Queue.empty();
}

Event EventQueue::Pop() {
    Event event = m_Queue.front();
    m_Queue.pop();
    return event;
}
