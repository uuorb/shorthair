/*
	Copyright (c) 2013 Christopher A. Taylor.  All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	* Neither the name of Shorthair nor the names of its contributors may be
	  used to endorse or promote products derived from this software without
	  specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef CAT_SHORTHAIR_HPP
#define CAT_SHORTHAIR_HPP

#include "ShorthairDetails.hpp"

/*
 * Shorthair Low-Latency Networking
 *
 * Shorthair provides channel modeling, security, and low-latency messaging.
 *
 * It allows you to take any UDP/IP data stream and dial the packet loss rate
 * down as low as you like, while preventing tampering, and providing live
 * numbers for loss and latency.
 *
 * In short, it fixes the largest problems with using UDP/IP.
 *
 * On top of Shorthair you can build any number of other transport protocols
 * involving ordered-reliable, ordered-unreliable, and unordered-unreliable
 * combinations.  As a black box you can treat it like normal UDP with some
 * nice additional features (security, very low loss) as otherwise it works
 * identically.
 *
 * Remaining problems:
 *
 * + Congestion control (CC) is not provided.
 * + You can still lose packets (rarely).
 * + Packets still arrive out of order.
 *
 * ARQ can be built effectively on top of Shorthair producing a hybrid that has
 * very low average latency and guaranteed delivery.  Sending a NACK over the
 * reverse channel as normal data is an effective way to recover from losses for
 * full reliability, whereas normal ARQ would suffer from over 2x the normal
 * packet loss rate to recover from a loss event.
 *
 * I decided to split the transport layer into shorthair + ARQ/CC since for a
 * lot of applications like audio streaming, ARQ and CC are not even desired.
 */

namespace cat {

namespace shorthair {


class IShorthair {
public:
	// Called with the latest data packet from remote host
	virtual void OnPacket(void *packet, int bytes) = 0;

	// Called with the latest OOB packet from remote host
	virtual void OnOOB(const u8 *packet, int bytes) = 0;

	// Send raw data to remote host over UDP socket
	virtual void SendData(void *buffer, int bytes) = 0;
};


struct Settings {
	// Did currrent instance initiate the data flow?
	// Each side of the channel needs to pick an opposite role to ensure that
	// the encryption works properly.
	bool initiator;				// true = Client mode, false = Server mode

	// Good default: 0.0001
	double target_loss;			// Target packet loss rate

	// Good default: 0.03
	float min_loss;				// [0..1] packetloss probability lower limit

	// Good default: 100 ms ... 2000 ms
	int min_delay, max_delay;	// Milliseconds clamp values for delay estimation

	// Good default: 1350 bytes
	int max_data_size;			// Maximum data size in bytes

	// Implement this interface to allow Shorthair to send and deliver packets
	IShorthair *interface;		// Interface
};


class Shorthair {
	// Initialized flag
	bool _initialized;

	// Timekeeping
	Clock _clock;

	// Settings object
	Settings _settings;

	// Packet buffers are allocated with room for the protocol overhead + data
	ReuseAllocator _allocator;

	// Encryption
	calico::Calico _cipher;

private:
	//// Encoder
	EncoderThread _encoder;

	// Statistics
	DelayEstimator _delay;
	LossEstimator _loss;

	// Code group currently being sent
	u8 _code_group;

	// Swap times for each code group for RTT calculation
	u32 _group_stamps[256];

	// Packet workspace buffer
	SmartArray<u8> _packet_buffer;

	// Rate of swapping and redundant symbol counter
	u32 _swap_interval;
	u32 _last_swap_time;
	int _redundant_count, _redundant_sent;

protected:
	// Send a check symbol
	bool SendCheckSymbol();

	// Calculate interval from delay
	void CalculateInterval();

	// From pong message, round-trip time
	void UpdateRTT(int ms);

	// From pong message, number of packets seen out of count in interval
	void UpdateLoss(u32 seen, u32 count);

	// On receiving an out-of-band packet
	void OnOOB(u8 *pkt, int len);

private:
	//// Decoder
	wirehair::Codec _decoder;

	// Is decoder active?
	bool _decoding;
	u8 _decoding_group;

	// Next expected code group
	u8 _largest_group;

	// Statistics since the last pong
	u32 _seen, _count;

	// Code groups
	CodeGroup _groups[256];

protected:
	void RecoverGroup(CodeGroup *group);

	// On receiving a data packet
	void OnData(u8 *pkt, int len);

	// Send collected statistics
	void SendPong(int code_group);

public:
	CAT_INLINE Shorthair() {
		_initialized = false;
	}

	CAT_INLINE virtual ~Shorthair() {
		Finalize();
	}

	// On startup:
	bool Initialize(const u8 key[SKEY_BYTES], const Settings &settings);

	// Cleanup
	void Finalize();

	// Send a new packet
	void Send(const void *data, int len);

	// Send an OOB packet, first byte is type code
	void SendOOB(const u8 *data, int len);

	// Called once per tick, about 10-20 ms
	void Tick();

	// On packet received
	void Recv(u8 *pkt, int len);
};


} // namespace shorthair

} // namespace cat

#endif // CAT_SHORTHAIR_HPP

