#define _WIN32_WINNT 0x0501
#include "../SDK/foobar2000.h"
#include "../ATLHelpers/ATLHelpers.h"
#include "resource.h"
#include "SoundTouch/SoundTouch.h"
#include "rubberband/rubberband/RubberBandStretcher.h"
#include "circular_buffer.h"
using namespace soundtouch;
using namespace RubberBand;

static void RunDSPConfigPopup( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback );
static void RunDSPConfigPopupRate( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback );
static void RunDSPConfigPopupTempo( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback );
#define BUFFER_SIZE 2048

class dsp_pitch : public dsp_impl_base
{
	SoundTouch * p_soundtouch;
	RubberBandStretcher * rubber;
	float **plugbuf;
	float **m_scratch;

	int m_rate, m_ch, m_ch_mask;
	float pitch_amount;
	circular_buffer<float>sample_buffer;
	pfc::array_t<float>samplebuf;
	unsigned buffered;
	bool st_enabled;
	int pitch_shifter;
private:
	void insert_chunks_rubber()
	{
		while (1)
		{
			t_size samples = rubber->available();
			if (samples <= 0)break;
			samples = rubber->retrieve(m_scratch, samples);
			if (samples > 0)
			{
				float *data = samplebuf.get_ptr();
				for (int c = 0; c < m_ch; ++c) {
					int j = 0;
					while (j < samples) {
						data[j * m_ch + c] = m_scratch[c][j];
						++j;
					}
				}
				audio_chunk * chunk = insert_chunk(samples*m_ch);
				chunk->set_data(data, samples, m_ch, m_rate);
			}
		}
	}


	void insert_chunks_st()
	{
		uint samples;
		soundtouch::SAMPLETYPE * src = samplebuf.get_ptr();
		do
		{
			samples = p_soundtouch->receiveSamples(src, BUFFER_SIZE);
			if (samples > 0)
			{
				audio_chunk * chunk = insert_chunk(samples * m_ch);
				chunk->set_channels(m_ch, m_ch_mask);
				chunk->set_data_32(src, samples, m_ch, m_rate);
			}
		} while (samples != 0);
	}

public:
	dsp_pitch( dsp_preset const & in ) : pitch_amount(0.00), m_rate( 0 ), m_ch( 0 ), m_ch_mask( 0 )
	{
		buffered=0;
		p_soundtouch = 0;
		rubber = 0;
		plugbuf = 0;
		m_scratch = 0;
		parse_preset( pitch_amount,pitch_shifter, in );
		st_enabled = true;
	}
	~dsp_pitch(){
		if (p_soundtouch)
		{
			delete p_soundtouch;
			p_soundtouch = 0;
		}
		

		if (rubber)
		{
			insert_chunks_rubber();
			delete rubber;
			if (plugbuf)delete plugbuf;
			if (m_scratch)delete m_scratch;
			plugbuf = 0;
			m_scratch = 0;
			rubber = 0;
		}
		

	}

	// Every DSP type is identified by a GUID.
	static GUID g_get_guid() {
		// Create these with guidgen.exe.
		// {A7FBA855-56D4-46AC-8116-8B2A8DF2FB34}
		static const GUID guid =
		{ 0xa7fba855, 0x56d4, 0x46ac, { 0x81, 0x16, 0x8b, 0x2a, 0x8d, 0xf2, 0xfb, 0x34 } };
		return guid;
	}

	static void g_get_name(pfc::string_base & p_out) {
		p_out = "Pitch Shift";
	}

	virtual void on_endoftrack(abort_callback & p_abort) {
		if (rubber)
		{
			insert_chunks_rubber();
		}
	}

	virtual void on_endofplayback(abort_callback & p_abort) {
        //same as flush, only at end of playback
		if (p_soundtouch && st_enabled)
		{
				insert_chunks_st();
				if (buffered)
				{
					sample_buffer.read(samplebuf.get_ptr(),buffered*m_ch);
					p_soundtouch->putSamples(samplebuf.get_ptr(),buffered);
					buffered = 0;
				}
				p_soundtouch->flush();
				insert_chunks_st();	
		}

		if (rubber&& st_enabled)
		{
			insert_chunks_rubber();
		}
	}

	// The framework feeds input to our DSP using this method.
	// Each chunk contains a number of samples with the same
	// stream characteristics, i.e. same sample rate, channel count
	// and channel configuration.
	virtual bool on_chunk(audio_chunk * chunk, abort_callback & p_abort) {
		t_size sample_count = chunk->get_sample_count();
		audio_sample * src = chunk->get_data();

		if ( chunk->get_srate() != m_rate || chunk->get_channels() != m_ch || chunk->get_channel_config() != m_ch_mask )
		{
			m_rate = chunk->get_srate();
			m_ch = chunk->get_channels();
			m_ch_mask = chunk->get_channel_config();
			sample_buffer.set_size(BUFFER_SIZE*m_ch);
			samplebuf.set_size(BUFFER_SIZE*m_ch);


			if (pitch_shifter == 1)
			{
				RubberBandStretcher::Options options = RubberBandStretcher::DefaultOptions;
				options |= RubberBandStretcher::OptionProcessRealTime | RubberBandStretcher::OptionPitchHighQuality;
				rubber = new RubberBandStretcher(m_rate, m_ch, options, 1.0, pow(2.0, pitch_amount / 12.0));
				if (!rubber) return 0;
				if (plugbuf)delete plugbuf;
				if (m_scratch)delete m_scratch;
				plugbuf = new float*[m_ch];
				m_scratch = new float*[m_ch];
				for (int c = 0; c < m_ch; ++c) plugbuf[c] = new float[BUFFER_SIZE];
				for (int c = 0; c < m_ch; ++c) m_scratch[c] = new float[BUFFER_SIZE];
				st_enabled = true;
			}

			if (pitch_shifter == 0)
			{
				p_soundtouch = new SoundTouch;
				if (!p_soundtouch) return 0;
				if (p_soundtouch)
				{
					p_soundtouch->setSampleRate(m_rate);
					p_soundtouch->setChannels(m_ch);
					p_soundtouch->setPitchSemiTones(pitch_amount);
					st_enabled = true;
					if (pitch_amount == 0)st_enabled = false;
					bool usequickseek = true;
					bool useaafilter = true; //seems clearer without it
					p_soundtouch->setSetting(SETTING_USE_QUICKSEEK, usequickseek);
					p_soundtouch->setSetting(SETTING_USE_AA_FILTER, useaafilter);
				}
			}
		}
		

		if (!st_enabled) return true;

		if (rubber) {
			while (sample_count > 0)
			{
				int toCauseProcessing = rubber->getSamplesRequired();
				int todo = min(toCauseProcessing - buffered, sample_count);
				sample_buffer.write(src, todo*m_ch);
				src += todo * m_ch;
				buffered += todo;
				sample_count -= todo;
				if (buffered == toCauseProcessing)
				{
					float*data = samplebuf.get_ptr();
					sample_buffer.read((float*)data, toCauseProcessing*m_ch);
					for (int c = 0; c < m_ch; ++c) {
						int j = 0;
						while (j < toCauseProcessing) {
							plugbuf[c][j] = data[j * m_ch + c];
							++j;
						}
					}
					rubber->process(plugbuf, toCauseProcessing, false);
					insert_chunks_rubber();
					buffered = 0;
				}
			}
		}

		if (p_soundtouch)
		{
			while (sample_count > 0)
			{
				int todo = min(BUFFER_SIZE - buffered, sample_count);
				sample_buffer.write(src, todo*m_ch);
				src += todo * m_ch;
				buffered += todo;
				sample_count -= todo;
				if (buffered == BUFFER_SIZE)
				{
					sample_buffer.read(samplebuf.get_ptr(), buffered*m_ch);
					p_soundtouch->putSamples(samplebuf.get_ptr(), buffered);
					insert_chunks_st();
					buffered = 0;
				}
			}
		}
		
		return false;
	}

	virtual void flush() {
		if (p_soundtouch){
			p_soundtouch->clear();
		}
		if (rubber)
		{
			insert_chunks_rubber();
		}
		m_rate = 0;
		m_ch = 0;
		buffered = 0;
		m_ch_mask = 0;
	}

	virtual double get_latency() {
		if (p_soundtouch)
		{
			return (m_rate && st_enabled) ? (double)(p_soundtouch->numSamples() + buffered) / (double)m_rate:0;
		}
		if (rubber)
		{
			return (m_rate && st_enabled) ? (double)(rubber->getLatency()) / (double)m_rate:0;
		}
		return 0;
	}


	virtual bool need_track_change_mark() {
		return false;
	}

	static bool g_get_default_preset( dsp_preset & p_out )
	{
		make_preset( 0.0,0, p_out );
		return true;
	}
	static void g_show_config_popup( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback )
	{
		::RunDSPConfigPopup( p_data, p_parent, p_callback );
	}
	static bool g_have_config_popup() { return true; }
	static void make_preset( float pitch,int pitch_type, dsp_preset & out )
	{
		dsp_preset_builder builder; 
		builder << pitch; 
		builder << pitch_type;
		builder.finish( g_get_guid(), out );
	}                        
	static void parse_preset(float & pitch,int & pitch_type, const dsp_preset & in)
	{
		try
		{
			dsp_preset_parser parser(in);
			parser >> pitch; 
			parser >> pitch_type;
		}
		catch (exception_io_data) { pitch = 0.0; pitch_type = 0; }
	}
};

class dsp_tempo : public dsp_impl_base
{
	SoundTouch * p_soundtouch;
	RubberBandStretcher * rubber;
	float **plugbuf;
	float **m_scratch;

	int m_rate, m_ch, m_ch_mask;
	float pitch_amount;
	circular_buffer<float>sample_buffer;
	pfc::array_t<float>samplebuf;
	unsigned buffered;
	bool st_enabled;
	int pitch_shifter;
private:
	void insert_chunks_rubber()
	{
		while (1)
		{
			t_size samples = rubber->available();
			if (samples <= 0)break;
			samples = rubber->retrieve(m_scratch, samples);
			if (samples > 0)
			{
				float *data = samplebuf.get_ptr();
				for (int c = 0; c < m_ch; ++c) {
					int j = 0;
					while (j < samples) {
						data[j * m_ch + c] = m_scratch[c][j];
						++j;
					}
				}
				audio_chunk * chunk = insert_chunk(samples*m_ch);
				chunk->set_data(data, samples, m_ch, m_rate);
			}
		}
	}


	void insert_chunks_st()
	{
		uint samples;
		soundtouch::SAMPLETYPE * src = samplebuf.get_ptr();
		do
		{
			samples = p_soundtouch->receiveSamples(src, BUFFER_SIZE);
			if (samples > 0)
			{
				audio_chunk * chunk = insert_chunk(samples * m_ch);
				chunk->set_channels(m_ch, m_ch_mask);
				chunk->set_data_32(src, samples, m_ch, m_rate);
			}
		} while (samples != 0);
	}


public:
	dsp_tempo( dsp_preset const & in ) : pitch_amount(0.00), m_rate( 0 ), m_ch( 0 ), m_ch_mask( 0 )
	{
		buffered = 0;
		p_soundtouch = 0;
		rubber = 0;
		plugbuf = 0;
		m_scratch = 0;
		parse_preset(pitch_amount, pitch_shifter, in);
		st_enabled = true;
	}
	~dsp_tempo(){
		if (p_soundtouch)
		{
			delete p_soundtouch;
			p_soundtouch = 0;
		}


		if (rubber)
		{
			insert_chunks_rubber();
			delete rubber;
			if (plugbuf)delete plugbuf;
			if (m_scratch)delete m_scratch;
			plugbuf = 0;
			m_scratch = 0;
			rubber = 0;
		}
	}

	// Every DSP type is identified by a GUID.
	static GUID g_get_guid() {
		// {44BCACA2-9EDD-493A-BB8F-9474F4B5A76B}
		static const GUID guid = 
		{ 0x44bcaca2, 0x9edd, 0x493a, { 0xbb, 0x8f, 0x94, 0x74, 0xf4, 0xb5, 0xa7, 0x6b } };
		return guid;
	}

	// We also need a name, so the user can identify the DSP.
	// The name we use here does not describe what the DSP does,
	// so it would be a bad name. We can excuse this, because it
	// doesn't do anything useful anyway.
	static void g_get_name(pfc::string_base & p_out) {
		p_out = "Tempo Shift";
	}

	virtual void on_endoftrack(abort_callback & p_abort) {
		if (rubber)
		{
			insert_chunks_rubber();
		}
	}

	virtual void on_endofplayback(abort_callback & p_abort) {
		//same as flush, only at end of playback
		if (p_soundtouch && st_enabled)
		{
			insert_chunks_st();
			if (buffered)
			{
				sample_buffer.read(samplebuf.get_ptr(), buffered*m_ch);
				p_soundtouch->putSamples(samplebuf.get_ptr(), buffered);
				buffered = 0;
			}
			p_soundtouch->flush();
			insert_chunks_st();
		}

		if (rubber&& st_enabled)
		{
			insert_chunks_rubber();
		}
	}

	// The framework feeds input to our DSP using this method.
	// Each chunk contains a number of samples with the same
	// stream characteristics, i.e. same sample rate, channel count
	// and channel configuration.


	virtual bool on_chunk(audio_chunk * chunk, abort_callback & p_abort) {
		t_size sample_count = chunk->get_sample_count();
		audio_sample * src = chunk->get_data();

		if (chunk->get_srate() != m_rate || chunk->get_channels() != m_ch || chunk->get_channel_config() != m_ch_mask)
		{
			m_rate = chunk->get_srate();
			m_ch = chunk->get_channels();
			m_ch_mask = chunk->get_channel_config();
			sample_buffer.set_size(BUFFER_SIZE*m_ch);
			samplebuf.set_size(BUFFER_SIZE*m_ch);


			if (pitch_shifter == 1)
			{
				RubberBandStretcher::Options options = RubberBandStretcher::DefaultOptions;
				options |= RubberBandStretcher::OptionProcessRealTime | RubberBandStretcher::OptionPitchHighQuality;
				rubber = new RubberBandStretcher(m_rate, m_ch, options, 1.0 + 0.01 *-pitch_amount, 1.0);
				if (!rubber) return 0;
				if (plugbuf)delete plugbuf;
				if (m_scratch)delete m_scratch;
				plugbuf = new float*[m_ch];
				m_scratch = new float*[m_ch];
				for (int c = 0; c < m_ch; ++c) plugbuf[c] = new float[BUFFER_SIZE];
				for (int c = 0; c < m_ch; ++c) m_scratch[c] = new float[BUFFER_SIZE];
				st_enabled = true;
			}

			if (pitch_shifter == 0)
			{
				p_soundtouch = new SoundTouch;
				if (!p_soundtouch) return 0;
				if (p_soundtouch)
				{
					p_soundtouch->setSampleRate(m_rate);
					p_soundtouch->setChannels(m_ch);
					p_soundtouch->setTempoChange(pitch_amount);
					st_enabled = true;
					if (pitch_amount == 0)st_enabled = false;
					bool usequickseek = true;
					bool useaafilter = true; //seems clearer without it
					p_soundtouch->setSetting(SETTING_USE_QUICKSEEK, usequickseek);
					p_soundtouch->setSetting(SETTING_USE_AA_FILTER, useaafilter);
				}
			}
		}


		if (!st_enabled) return true;

		if (rubber) {
			while (sample_count > 0)
			{
				int toCauseProcessing = rubber->getSamplesRequired();
				int todo = min(toCauseProcessing - buffered, sample_count);
				sample_buffer.write(src, todo*m_ch);
				src += todo * m_ch;
				buffered += todo;
				sample_count -= todo;
				if (buffered == toCauseProcessing)
				{
					float*data = samplebuf.get_ptr();
					sample_buffer.read((float*)data, toCauseProcessing*m_ch);
					for (int c = 0; c < m_ch; ++c) {
						int j = 0;
						while (j < toCauseProcessing) {
							plugbuf[c][j] = data[j * m_ch + c];
							++j;
						}
					}
					rubber->process(plugbuf, toCauseProcessing, false);
					insert_chunks_rubber();
					buffered = 0;
				}
			}
		}

		if (p_soundtouch)
		{
			while (sample_count > 0)
			{
				int todo = min(BUFFER_SIZE - buffered, sample_count);
				sample_buffer.write(src, todo*m_ch);
				src += todo * m_ch;
				buffered += todo;
				sample_count -= todo;
				if (buffered == BUFFER_SIZE)
				{
					sample_buffer.read(samplebuf.get_ptr(), buffered*m_ch);
					p_soundtouch->putSamples(samplebuf.get_ptr(), buffered);
					insert_chunks_st();
					buffered = 0;
				}
			}
		}

		return false;
	}

	virtual void flush() {
		if (p_soundtouch) {
			p_soundtouch->clear();
		}
		if (rubber)
		{
			insert_chunks_rubber();
		}
		m_rate = 0;
		m_ch = 0;
		buffered = 0;
		m_ch_mask = 0;
	}

	virtual double get_latency() {
		if (p_soundtouch)
		{
			return (m_rate && st_enabled) ? (double)(p_soundtouch->numSamples() + buffered) / (double)m_rate : 0;
		}
		if (rubber)
		{
			return (m_rate && st_enabled) ? (double)(rubber->getLatency()) / (double)m_rate : 0;
		}
		return 0;
	}


	virtual bool need_track_change_mark() {
		return false;
	}

	static bool g_get_default_preset(dsp_preset & p_out)
	{
		make_preset(0.0, 0, p_out);
		return true;
	}
	static void g_show_config_popup( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback )
	{
		::RunDSPConfigPopupTempo( p_data, p_parent, p_callback );
	}
	static bool g_have_config_popup() { return true; }
	static void make_preset(float pitch, int pitch_type, dsp_preset & out)
	{
		dsp_preset_builder builder;
		builder << pitch;
		builder << pitch_type;
		builder.finish(g_get_guid(), out);
	}
	static void parse_preset(float & pitch, int & pitch_type, const dsp_preset & in)
	{
		try
		{
			dsp_preset_parser parser(in);
			parser >> pitch;
			parser >> pitch_type;
		}
		catch (exception_io_data) { pitch = 0.0; pitch_type = 0; }
	}
};

class dsp_rate : public dsp_impl_base
{
	SoundTouch * p_soundtouch;
	int m_rate, m_ch, m_ch_mask;
	float pitch_amount;
	circular_buffer<soundtouch::SAMPLETYPE>sample_buffer;
	pfc::array_t<soundtouch::SAMPLETYPE>samplebuf;
	unsigned buffered;
	bool st_enabled;
private:
	void insert_chunks()
	{
		uint samples = p_soundtouch->numSamples();
		if (!samples) return;
		samplebuf.grow_size(BUFFER_SIZE * m_ch);
		soundtouch::SAMPLETYPE * src = samplebuf.get_ptr();
		do
		{
			samples = p_soundtouch->receiveSamples(src, BUFFER_SIZE);
			if (samples > 0)
			{
				audio_chunk * chunk = insert_chunk(samples * m_ch);
				//	chunk->set_channels(m_ch,m_ch_mask);
				chunk->set_data_32(src, samples, m_ch, m_rate);
			}
		}
		while (samples != 0);
	}

public:
	dsp_rate( dsp_preset const & in ) : pitch_amount(0.00), m_rate( 0 ), m_ch( 0 ), m_ch_mask( 0 )
	{
		p_soundtouch=0;
		buffered=0;
		parse_preset( pitch_amount, in );
		st_enabled = true;
	}
	~dsp_rate(){
		if (p_soundtouch) delete p_soundtouch;
		p_soundtouch = 0;
	}

	// Every DSP type is identified by a GUID.
	static GUID g_get_guid() {
		// {8C12D81E-BB88-4056-B4C0-EAFA4E9F3B95}
		static const GUID guid = 
		{ 0x8c12d81e, 0xbb88, 0x4056, { 0xb4, 0xc0, 0xea, 0xfa, 0x4e, 0x9f, 0x3b, 0x95 } };
		return guid;
	}

	// We also need a name, so the user can identify the DSP.
	// The name we use here does not describe what the DSP does,
	// so it would be a bad name. We can excuse this, because it
	// doesn't do anything useful anyway.
	static void g_get_name(pfc::string_base & p_out) {
		p_out = "Playback Rate Shift";
	}

	virtual void on_endoftrack(abort_callback & p_abort) {
		// This method is called when a track ends.
		// We need to do the same thing as flush(), so we just call it.

	}

	virtual void on_endofplayback(abort_callback & p_abort) {
		// This method is called on end of playback instead of flush().
		// We need to do the same thing as flush(), so we just call it.
		if (p_soundtouch && st_enabled)
		{
			insert_chunks();
			if (buffered)
			{
				sample_buffer.read(samplebuf.get_ptr(),buffered*m_ch);
				p_soundtouch->putSamples(samplebuf.get_ptr(),buffered);
				buffered = 0;
			}
			p_soundtouch->flush();
			insert_chunks();
		}
	}

	// The framework feeds input to our DSP using this method.
	// Each chunk contains a number of samples with the same
	// stream characteristics, i.e. same sample rate, channel count
	// and channel configuration.
	virtual bool on_chunk(audio_chunk * chunk, abort_callback & p_abort) {
		t_size sample_count = chunk->get_sample_count();
		audio_sample * src = chunk->get_data();

		if ( chunk->get_srate() != m_rate || chunk->get_channels() != m_ch || chunk->get_channel_config() != m_ch_mask )
		{
			m_rate = chunk->get_srate();
			m_ch = chunk->get_channels();
			m_ch_mask = chunk->get_channel_config();
			p_soundtouch = new SoundTouch;
			sample_buffer.set_size(BUFFER_SIZE*m_ch);
			if (!p_soundtouch) return 0;
			p_soundtouch->setSampleRate(m_rate);
			p_soundtouch->setChannels(m_ch);
			p_soundtouch->setRateChange(pitch_amount);
			st_enabled = true;
			if (pitch_amount== 0)st_enabled = false;
		}
		samplebuf.grow_size(BUFFER_SIZE * m_ch);
		if (!st_enabled) return true;
		while (sample_count)
		{    
			int todo = min(BUFFER_SIZE - buffered, sample_count);
			sample_buffer.write(src,todo*m_ch);
			src += todo * m_ch;
			buffered += todo;
			sample_count -= todo;
			if (buffered == BUFFER_SIZE)
			{
				sample_buffer.read(samplebuf.get_ptr(),buffered*m_ch);
				p_soundtouch->putSamples(samplebuf.get_ptr(), buffered);
				buffered = 0;
				insert_chunks();
			}
		}
		return false;
	}

	virtual void flush() {
		if (p_soundtouch){
			p_soundtouch->clear();
		}
		m_rate = 0;
		m_ch = 0;
		m_ch_mask = 0;
	}

	virtual double get_latency() {
		return (p_soundtouch && m_rate && st_enabled) ? ((double)(p_soundtouch->numSamples() + buffered) / (double)m_rate) : 0;
	}

	virtual bool need_track_change_mark() {
		return false;
	}

	static bool g_get_default_preset( dsp_preset & p_out )
	{
		make_preset( 0.0, p_out );
		return true;
	}
	static void g_show_config_popup( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback )
	{
		::RunDSPConfigPopupRate( p_data, p_parent, p_callback );
	}
	static bool g_have_config_popup() { return true; }
	static void make_preset( float pitch, dsp_preset & out )
	{
		dsp_preset_builder builder; 
		builder << pitch; 
		builder.finish( g_get_guid(), out );
	}                        
	static void parse_preset(float & pitch, const dsp_preset & in)
	{
		try
		{
			dsp_preset_parser parser(in);
			parser >> pitch; 
		}
		catch(exception_io_data) {pitch = 0.0;}
	}
};

class CMyDSPPopupPitch : public CDialogImpl<CMyDSPPopupPitch>
{
public:
	CMyDSPPopupPitch( const dsp_preset & initData, dsp_preset_edit_callback & callback ) : m_initData( initData ), m_callback( callback ) { }
	enum { IDD = IDD_PITCH };
	enum
	{
		pitchmin = 0,
		pitchmax = 4800
		
	};
	BEGIN_MSG_MAP( CMyDSPPopup )
		MSG_WM_INITDIALOG( OnInitDialog )
		COMMAND_HANDLER_EX( IDOK, BN_CLICKED, OnButton )
		COMMAND_HANDLER_EX( IDCANCEL, BN_CLICKED, OnButton )
		COMMAND_HANDLER_EX(IDC_PITCHTYPE, CBN_SELCHANGE, OnChange)
		MSG_WM_HSCROLL( OnChange )
	END_MSG_MAP()
private:
	BOOL OnInitDialog(CWindow, LPARAM)
	{
		slider_drytime = GetDlgItem(IDC_PITCH);
		slider_drytime.SetRange(0, pitchmax);

		CWindow w = GetDlgItem(IDC_PITCHTYPE);
		uSendMessageText(w, CB_ADDSTRING, 0, "SoundTouch");
		uSendMessageText(w, CB_ADDSTRING, 0, "Rubber Band");
		int pitch_type;
	
		{


			float  pitch;
			dsp_pitch::parse_preset(pitch,pitch_type, m_initData);
			::SendMessage(w, CB_SETCURSEL, pitch_type, 0);
			pitch *= 100.00;
			slider_drytime.SetPos( (double)(pitch+2400));
			RefreshLabel( pitch/100.00);
		}
		return TRUE;
	}

	void OnButton( UINT, int id, CWindow )
	{
		EndDialog( id );
	}

	void OnChange(UINT, int id, CWindow)
	{
			float pitch;
			pitch = slider_drytime.GetPos() - 2400;
			pitch /= 100.00;


			int p_type; //filter type
			p_type = SendDlgItemMessage(IDC_PITCHTYPE, CB_GETCURSEL);
			{
				dsp_preset_impl preset;
				dsp_pitch::make_preset(pitch, p_type, preset);
				m_callback.on_preset_changed(preset);
			}
			RefreshLabel(pitch);
	}
	

	void RefreshLabel(float  pitch )
	{
		pfc::string_formatter msg; 
		msg << "Pitch: ";
		msg << (pitch < 0 ? "" : "+");
		msg << pfc::format_float(pitch,0,2) << " semitones";
		::uSetDlgItemText( *this, IDC_PITCHINFO, msg );
		msg.reset();
	}
	const dsp_preset & m_initData; // modal dialog so we can reference this caller-owned object.
	dsp_preset_edit_callback & m_callback;
	CTrackBarCtrl slider_drytime,slider_wettime,slider_dampness,slider_roomwidth,slider_roomsize;
};
static void RunDSPConfigPopup( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback )
{
	CMyDSPPopupPitch popup( p_data, p_callback );
	if ( popup.DoModal(p_parent) != IDOK ) p_callback.on_preset_changed( p_data );
}

class CMyDSPPopupRate : public CDialogImpl<CMyDSPPopupRate>
{
public:
	CMyDSPPopupRate( const dsp_preset & initData, dsp_preset_edit_callback & callback ) : m_initData( initData ), m_callback( callback ) { }
	enum { IDD = IDD_RATE };
	enum
	{
		pitchmin = 0,
		pitchmax = 150

	};
	BEGIN_MSG_MAP( CMyDSPPopup )
		MSG_WM_INITDIALOG( OnInitDialog )
		COMMAND_HANDLER_EX( IDOK, BN_CLICKED, OnButton )
		COMMAND_HANDLER_EX( IDCANCEL, BN_CLICKED, OnButton )
		MSG_WM_HSCROLL( OnHScroll )
	END_MSG_MAP()
private:
	BOOL OnInitDialog(CWindow, LPARAM)
	{
		slider_drytime = GetDlgItem(IDC_RATE);
		slider_drytime.SetRange(0, pitchmax);

		{
			float  pitch;
			dsp_rate::parse_preset(pitch, m_initData);
			slider_drytime.SetPos( (double)(pitch+50));
			RefreshLabel( pitch);
		}
		return TRUE;
	}

	void OnButton( UINT, int id, CWindow )
	{
		EndDialog( id );
	}

	void OnHScroll( UINT nSBCode, UINT nPos, CScrollBar pScrollBar )
	{
		float pitch;
		pitch = slider_drytime.GetPos()-50;
		{
			dsp_preset_impl preset;
			dsp_rate::make_preset(pitch, preset );
			m_callback.on_preset_changed( preset );
		}
		RefreshLabel( pitch);
	}

	void RefreshLabel(float  pitch )
	{
		pfc::string_formatter msg; 
		msg << "Playback Rate: ";
		msg << (pitch < 0 ? "" : "+");
		msg << pfc::format_int( pitch) << "%";
		::uSetDlgItemText( *this, IDC_RATEINFO, msg );
		msg.reset();
	}
	const dsp_preset & m_initData; // modal dialog so we can reference this caller-owned object.
	dsp_preset_edit_callback & m_callback;
	CTrackBarCtrl slider_drytime,slider_wettime,slider_dampness,slider_roomwidth,slider_roomsize;
};
static void RunDSPConfigPopupRate( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback )
{
	CMyDSPPopupRate popup( p_data, p_callback );
	if ( popup.DoModal(p_parent) != IDOK ) p_callback.on_preset_changed( p_data );
}

class CMyDSPPopupTempo : public CDialogImpl<CMyDSPPopupTempo>
{
public:
	CMyDSPPopupTempo( const dsp_preset & initData, dsp_preset_edit_callback & callback ) : m_initData( initData ), m_callback( callback ) { }
	enum { IDD = IDD_TEMPO };
	enum
	{
		pitchmin = 0,
		pitchmax = 150

	};
	BEGIN_MSG_MAP( CMyDSPPopup )
		MSG_WM_INITDIALOG( OnInitDialog )
		COMMAND_HANDLER_EX( IDOK, BN_CLICKED, OnButton )
		COMMAND_HANDLER_EX( IDCANCEL, BN_CLICKED, OnButton )
		COMMAND_HANDLER_EX(IDC_TEMPOTYPE, CBN_SELCHANGE, OnChange)
		MSG_WM_HSCROLL(OnChange)
	END_MSG_MAP()
private:
	BOOL OnInitDialog(CWindow, LPARAM)
	{
		slider_drytime = GetDlgItem(IDC_TEMPO);
		slider_drytime.SetRange(0, pitchmax);



		CWindow w = GetDlgItem(IDC_TEMPOTYPE);
		uSendMessageText(w, CB_ADDSTRING, 0, "SoundTouch");
		uSendMessageText(w, CB_ADDSTRING, 0, "Rubber Band");
		int pitch_type;
		{
			float  pitch;
			dsp_tempo::parse_preset(pitch,pitch_type, m_initData);
			::SendMessage(w, CB_SETCURSEL, pitch_type, 0);
			slider_drytime.SetPos( (double)(pitch+75));
			RefreshLabel( pitch);
		}
		return TRUE;
	}

	void OnButton( UINT, int id, CWindow )
	{
		EndDialog( id );
	}

	void OnChange(UINT, int id, CWindow)
	{
		float pitch;
		pitch = slider_drytime.GetPos() - 75;
		int p_type; //filter type
		p_type = SendDlgItemMessage(IDC_TEMPOTYPE, CB_GETCURSEL);
		{
			dsp_preset_impl preset;
			dsp_tempo::make_preset(pitch, p_type, preset);
			m_callback.on_preset_changed(preset);
		}
		RefreshLabel(pitch);
	}


	void RefreshLabel(float  pitch )
	{
		pfc::string_formatter msg; 
		msg << "Tempo: ";
		msg << (pitch < 0 ? "" : "+");
		msg << pfc::format_int( pitch) << "%";
		::uSetDlgItemText( *this, IDC_TEMPOINFO, msg );
		msg.reset();
	}
	const dsp_preset & m_initData; // modal dialog so we can reference this caller-owned object.
	dsp_preset_edit_callback & m_callback;
	CTrackBarCtrl slider_drytime;
};
static void RunDSPConfigPopupTempo( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback )
{
	CMyDSPPopupTempo popup( p_data, p_callback );
	if ( popup.DoModal(p_parent) != IDOK ) p_callback.on_preset_changed( p_data );
}



static dsp_factory_t<dsp_tempo> g_dsp_tempo_factory;
static dsp_factory_t<dsp_pitch> g_dsp_pitch_factory;
static dsp_factory_t<dsp_rate> g_dsp_rate_factory;