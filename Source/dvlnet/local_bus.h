#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <utility>

#include "dvlnet/abstract_net.h"
#include "multi.h"

namespace devilution::net {

/**
 * In-process mailbox bus for local split-screen co-op.
 *
 * One mailbox per local player slot. Payloads are copied when submitted, so the
 * sender can reuse its buffer immediately and a receiver never holds a pointer
 * into another client's state. There are no threads: the session scheduler polls
 * each endpoint, so delivery order is deterministic.
 *
 * The bus deliberately does not implement discovery, connection negotiation or
 * encryption. A local session is created by the session coordinator, which knows
 * every seat; those layers stay on the external network providers.
 */
class local_bus {
public:
	/// Player slot, or MAX_PLRS when the value is not an endpoint.
	using endpoint = uint8_t;

	static local_bus &Instance();

	/// Claims the first free slot. Returns MAX_PLRS when the bus is full.
	endpoint attach();
	void detach(endpoint slot);
	[[nodiscard]] bool is_attached(endpoint slot) const;

	/// Copies the payload into the destination mailbox.
	bool send(endpoint from, endpoint to, const buffer_t &payload);
	/// Copies the payload into every attached mailbox except the sender's.
	void broadcast(endpoint from, const buffer_t &payload);

	bool recv(endpoint slot, endpoint &from, buffer_t &payload);

	/// Peers that detached since the last call for this mailbox.
	std::deque<endpoint> take_disconnected(endpoint slot);

	/// Test hook: forget all mailboxes.
	void reset();

private:
	struct mailbox {
		bool attached = false;
		std::deque<std::pair<endpoint, buffer_t>> queue;
		std::deque<endpoint> disconnected;
	};

	std::array<mailbox, MAX_PLRS> boxes_ {};
};

} // namespace devilution::net