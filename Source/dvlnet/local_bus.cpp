#include "dvlnet/local_bus.h"

namespace devilution::net {

namespace {
local_bus instance;
} // namespace

local_bus &local_bus::Instance()
{
	return instance;
}

local_bus::endpoint local_bus::attach()
{
	for (size_t i = 0; i < boxes_.size(); ++i) {
		if (boxes_[i].attached)
			continue;
		boxes_[i] = {};
		boxes_[i].attached = true;
		return static_cast<endpoint>(i);
	}
	return static_cast<endpoint>(MAX_PLRS);
}

void local_bus::detach(endpoint slot)
{
	if (slot >= boxes_.size() || !boxes_[slot].attached)
		return;
	boxes_[slot].attached = false;
	boxes_[slot].queue.clear();
	for (size_t i = 0; i < boxes_.size(); ++i) {
		if (i == slot || !boxes_[i].attached)
			continue;
		boxes_[i].disconnected.push_back(slot);
	}
}

bool local_bus::is_attached(endpoint slot) const
{
	return slot < boxes_.size() && boxes_[slot].attached;
}

bool local_bus::send(endpoint from, endpoint to, const buffer_t &payload)
{
	if (!is_attached(from) || !is_attached(to))
		return false;
	boxes_[to].queue.emplace_back(from, payload);
	return true;
}

void local_bus::broadcast(endpoint from, const buffer_t &payload)
{
	if (!is_attached(from))
		return;
	for (size_t i = 0; i < boxes_.size(); ++i) {
		if (i == from || !boxes_[i].attached)
			continue;
		boxes_[i].queue.emplace_back(from, payload);
	}
}

bool local_bus::recv(endpoint slot, endpoint &from, buffer_t &payload)
{
	if (!is_attached(slot) || boxes_[slot].queue.empty())
		return false;
	from = boxes_[slot].queue.front().first;
	payload = std::move(boxes_[slot].queue.front().second);
	boxes_[slot].queue.pop_front();
	return true;
}

std::deque<local_bus::endpoint> local_bus::take_disconnected(endpoint slot)
{
	if (slot >= boxes_.size())
		return {};
	std::deque<endpoint> result = std::move(boxes_[slot].disconnected);
	boxes_[slot].disconnected.clear();
	return result;
}

void local_bus::reset()
{
	for (mailbox &box : boxes_)
		box = {};
}

} // namespace devilution::net