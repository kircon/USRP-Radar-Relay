/*
 * Copyright (c) 2012 Neratec Solutions AG
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <iostream>
#include <complex>

#include "Debug.hpp"
#include "UsrpRadarRelay.hpp"
#include <uhd/utils/thread_priority.hpp>

UsrpRadarRelay::UsrpRadarRelay()
{
	DLOG << "Creating the usrp device..." << std::endl;
	usrp = uhd::usrp::multi_usrp::make(std::string(""));
	DLOG << "Using Device: " << usrp->get_pp_string() << std::endl;

	num_channels = usrp->get_tx_num_channels();

	operating_freq = 0.0;
	operating_rate = 0.0;
	operating_gain = 0.0;
	pattern_repeats = 0;
	uhd::set_thread_priority_safe();
	//Modification to use external clock instead of internal
	mboard = 0;
	clock_source = "";
	master_clock_rate = 52e6;
	operating_mcr = 0.0;
	//Modification end
}

UsrpRadarRelay::~UsrpRadarRelay()
{
	// terminate TX stream;
	md.end_of_burst = true;
	tx_stream->send("", 0, md);
}

bool UsrpRadarRelay::create_pulse_pattern(struct pulse_pattern *pattern)
{
	uint repeat_cycles = 1;
	if (pattern->repeats == -1) {
		/* ensure continuous patterns span at least 10ms */
		repeat_cycles = (1e4 / pattern->repeat_interval) + 1;
		tx_samples = operating_rate * pattern->repeat_interval;
		tx_samples *= repeat_cycles;
		DLOG << "repeat_cycles = " << repeat_cycles << std::endl;
	}
	else
		tx_samples = operating_rate * pattern->duration;

	DLOG << "creating pattern with " << tx_samples
			<< " tx_samples" << std::endl;
	pattern_buff.assign(tx_samples, std::complex<float>(0.0, 0.0));
	int t0 = pattern->pulses[0].ts;
	for (uint j = 0; j < repeat_cycles; j++) {
		for (uint i=0; i < pattern->num_pulses; i++) {
			struct pulse *p = &pattern->pulses[i];
			std::complex<float> hi =
					std::complex<float>(p->ampl, p->ampl);
			uint ts = p->ts - t0 + j * pattern->repeat_interval;
			uint sample_offset = (operating_rate * ts);
			uint p_samples = round(operating_rate * p->width);
			DLOG	<< "add_pulse(" << ts << ", " << p->width
				<< "): " << p_samples << " samples at offset "
				<< sample_offset << std::endl;
			for (uint k=0; k < p_samples; k++)
				pattern_buff[sample_offset + k] = hi;
		}
	}
	pattern_repeats = pattern->repeats;
	if (pattern_repeats == 0)
		pattern_repeats = 1;
	return true;
}

bool UsrpRadarRelay::configure_tx(double freq, double rate, double gain)
{

	//Modification to use external clock instead of internal
	DLOG << "Setting external Clock..." << std::endl;
	usrp->set_clock_source(std::string("external"));
	clock_source = usrp->get_clock_source(mboard);
	DLOG << "Clock source: " << clock_source << std::endl;
	DLOG << "Setting Master Clock Rate to " << master_clock_rate/1e6 << " MHz"<< std::endl;
	usrp->set_master_clock_rate(master_clock_rate);
	operating_mcr = usrp->get_master_clock_rate(mboard) / 1e6;
	DLOG << "Master Clock Rate: " << operating_mcr << " MHz" << std::endl;
	//Modification end

	uint i;
	DLOG << "Setting TX Rate to " << rate << " Msps...\t";
	usrp->set_tx_rate(rate * 1e6);
	operating_rate = usrp->get_tx_rate() / 1e6;
	DLOG << "operating TX Rate: " << operating_rate << " Msps" << std::endl;

	DLOG << "Setting TX Freq to " << freq << " MHz...\t";
	for (i = 0; i < num_channels; i++)
		usrp->set_tx_freq(freq * 1e6);
	operating_freq = usrp->get_tx_freq() / 1e6;
	DLOG << "operating TX Freq: " << operating_freq << " MHz" << std::endl;

	DLOG << "Setting TX Gain to " << gain << " ......\t";
	for (i = 0; i < num_channels; i++)
		usrp->set_tx_gain(gain, i);
	operating_gain = usrp->get_tx_gain();
	DLOG << "operating TX Gain: " << operating_gain << std::endl;

	tx_stream = usrp->get_tx_stream(uhd::stream_args_t("fc32"));
	usrp->set_time_now(uhd::time_spec_t(0.0));

	pattern_repeats = 0;

	return true;
}

