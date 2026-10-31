/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Looper.h>
#include <Message.h>
#include <Messenger.h>

#include <functional>

namespace sen {
namespace ui {

/**
 * Work that takes too long for the thread of a window or of the server (queries over all volumes, the installer of the ontologies): a looper of its own, so that
 * nobody is held up. The work is a function that is posted to the looper as a message: the looper handles one at a time, in the order in which they were posted,
 * and sends the message that the function returns to the target, which receives the result like any other message (an object that the result needs to carry can be
 * put into it as a pointer, and is freed by the receiver; for the case that the target is gone there is a function that frees it). A window or the server
 * has one worker, and tells it to finish when it is done with it.
 */
class Worker : public BLooper {
public:
	/** the work: it returns the message with the result for the target (it is deleted after it was sent), or NULL if there is no result */
	typedef std::function<BMessage*()> Work;
	/** called with the message of a result that could not be delivered (the window is gone): frees what it carries */
	typedef std::function<void(BMessage*)> Discard;

	Worker(const char* name, int32 priority = B_LOW_PRIORITY)
		:
		BLooper(name, priority)
	{
		Run();
	}

	/** @return B_OK if the work is queued */
	status_t
	Post(const Work& work, const BMessenger& target, const Discard& discard = Discard())
	{
		BMessage message(kWork);
		Job* job = new Job{work, target, discard};
		message.AddPointer("job", job);
		status_t result = PostMessage(&message);
		if (result != B_OK)
			delete job;
		return result;
	}

	/** The worker quits when the work that is queued is done (a window that is closed does not wait for it). */
	void
	Finish()
	{
		PostMessage(B_QUIT_REQUESTED);
	}

	virtual void
	MessageReceived(BMessage* message)
	{
		if (message->what != kWork) {
			BLooper::MessageReceived(message);
			return;
		}

		Job* job = NULL;
		if (message->FindPointer("job", (void**)&job) != B_OK || job == NULL)
			return;
		BMessage* result = job->work();
		if (result != NULL) {
			if (job->target.SendMessage(result) != B_OK && job->discard)
				job->discard(result);
			delete result;
		}
		delete job;
	}

private:
	static const uint32 kWork = 'Swrk';

	struct Job {
		Work		work;
		BMessenger	target;
		Discard		discard;
	};
};

}	// namespace ui
}	// namespace sen
