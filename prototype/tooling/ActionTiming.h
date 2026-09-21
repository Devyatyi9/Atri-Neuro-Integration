#pragma once

#include <string>

// Where an action's time goes, from the moment it arrives to the moment its
// result leaves.
//
// The total is known -- it is measured from the harness, which timestamps both
// ends -- but not how it splits. This names the pieces the plugin can see: the
// handler's own work, the crossings to the game's main thread inside it, and
// the wait between queueing a result and the poll thread sending it.
//
// One action at a time, which is how the server sends them: it waits for the
// result before sending another. A second action arriving mid-flight simply
// replaces the record, and the line for the first one is lost rather than being
// wrong.
//
// Off unless log_action_timing is on, because it is one more line per action
// and a route sends thousands.

// The action arrived. `name` is only for the line at the end.
void ActionTimingStart(std::string const &id, std::string const &name);

// The handler queued its result. Called from wherever the result is handed to
// the client, so the handler's own work is what lies between this and the
// start.
void ActionTimingResultQueued(std::string const &id);

// The result went out over the socket. Writes the line.
void ActionTimingResultSent(std::string const &id);

// One crossing to the game's main thread, as the two instants it lies between.
// Counted only for the thread that is handling the action, so the continuous
// hook's own calls do not land in someone else's account. The instants are
// kept, not just the duration: what is being looked for is the time an action
// spends outside its crossings, and that needs to know where the first one
// started and the last one ended.
void ActionTimingTjsSpan(long long start, long long end);

// The clock the spans are measured on, for a caller that wants to note an
// instant of its own.
long long ActionTimingNow();

// The same, measured for a scope: declared at the top of a call that crosses to
// the main thread, it reports the crossing when the call returns, by whichever
// of its several paths.
class ActionTimingCrossing {
 public:
	ActionTimingCrossing();
	~ActionTimingCrossing();

	ActionTimingCrossing(ActionTimingCrossing const &) = delete;
	ActionTimingCrossing &operator=(ActionTimingCrossing const &) = delete;

 private:
	long long start_;
};
