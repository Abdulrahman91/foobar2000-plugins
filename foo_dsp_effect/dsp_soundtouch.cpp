#define _WIN32_WINNT 0x0501
#include "../SDK/foobar2000.h"
#include "../ATLHelpers/ATLHelpers.h"
#include "resource.h"
#include "SoundTouch/SoundTouch.h"
#include "rubberband/rubberband/RubberBandStretcher.h"
#include "circular_buffer.h"
#include "dsp_guids.h"
using namespace soundtouch;
using namespace RubberBand;

static void RunDSPConfigPopup( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback );
static void RunDSPConfigPopupRate( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback );
static void RunDSPConfigPopupTempo( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback );
#define BUFFER_SIZE 2048
#define BUFFER_SIZE_RBTEMPO 1024

static double clamp_ml(double x, double upper, double lower)
{
    return min(upper, max(x, lower));
}

class CEditMod : public CWindowImpl<CEditMod, CEdit >
{
public:
   BEGIN_MSG_MAP(CEditMod)
      MESSAGE_HANDLER(WM_CHAR, OnChar)
   END_MSG_MAP()

   CEditMod(HWND hWnd = NULL) { }
   LRESULT OnChar(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
   {
      switch (wParam)
      {
      case '\r': //Carriage return
         ::PostMessage(m_parent, WM_USER, 0x1988, 0L);
         return 0;
         break;
      }
      return DefWindowProc(uMsg, wParam, lParam);
   }
   void AttachToDlgItem(HWND parent)
   {
      m_parent = parent;
   }
private:
   UINT m_dlgItem;
   HWND m_parent;
};

class dsp_pitch : public dsp_impl_base
{
	SoundTouch * p_soundtouch;
	float* buffer_;
	int m_rate, m_ch, m_ch_mask;
	float pitch_amount;
	bool st_enabled;

public:
	dsp_pitch(dsp_preset const & in) : pitch_amount(0.00), m_rate(0), m_ch(0), m_ch_mask(0)
	{
		p_soundtouch = 0;
		st_enabled = false;
		parse_preset(pitch_amount, st_enabled, in);
		buffer_ = NULL;
	}
	~dsp_pitch() {
		if (p_soundtouch)
		{
			p_soundtouch->clear();
			delete[] buffer_;
			buffer_ = NULL;
			delete p_soundtouch;
			p_soundtouch = 0;
		}
	}

	// Every DSP type is identified by a GUID.
	static GUID g_get_guid() {
		// Create these with guidgen.exe.
		// {A7FBA855-56D4-46AC-8116-8B2A8DF2FB34}

		return guid_pitch;
	}

	static void g_get_name(pfc::string_base & p_out) {
		p_out = "Pitch Shift";
	}

	virtual void on_endoftrack(abort_callback & p_abort) {
	}

	virtual void on_endofplayback(abort_callback & p_abort) {
		//same as flush, only at end of playback
	}

	// The framework feeds input to our DSP using this method.
	// Each chunk contains a number of samples with the same
	// stream characteristics, i.e. same sample rate, channel count
	// and channel configuration.
	virtual bool on_chunk(audio_chunk * chunk, abort_callback & p_abort) {
		t_size sample_count = chunk->get_sample_count();
		audio_sample * src = chunk->get_data();

        if (pitch_amount == 0.0)
        {
            st_enabled = false;
            return true;
        }
		if (!st_enabled) st_enabled = true;
       

		if (chunk->get_srate() != m_rate || chunk->get_channels() != m_ch || chunk->get_channel_config() != m_ch_mask)
		{

			m_rate = chunk->get_srate();
			m_ch = chunk->get_channels();
			m_ch_mask = chunk->get_channel_config();
			if (p_soundtouch)
			{
				delete[] buffer_;
				delete p_soundtouch;
			}

			buffer_ = new float[BUFFER_SIZE*m_ch];
			p_soundtouch = new SoundTouch;
			if (!p_soundtouch) return 0;
			if (p_soundtouch)
			{
				p_soundtouch->setSampleRate(m_rate);
				p_soundtouch->setChannels(m_ch);
				p_soundtouch->setPitchSemiTones(pitch_amount);
				bool usequickseek = true;
				bool useaafilter = true; //seems clearer without it
				p_soundtouch->setSetting(SETTING_USE_QUICKSEEK, true);
				p_soundtouch->setSetting(SETTING_USE_AA_FILTER, useaafilter);
			}
		}


		if (p_soundtouch)
		{
			t_size sample_count = chunk->get_sample_count();
			size_t out_samples_gen = BUFFER_SIZE;
			audio_sample * current = chunk->get_data();
			p_soundtouch->putSamples(current, sample_count);
			while (out_samples_gen > 0)
			{
				out_samples_gen = p_soundtouch->receiveSamples(buffer_, out_samples_gen);
				if (out_samples_gen != 0)
				{
					audio_chunk * chunk = insert_chunk(out_samples_gen * m_ch);
					chunk->set_data(buffer_, out_samples_gen, m_ch, m_rate, m_ch_mask);
				}
			}
		}

		return false;
	}

	virtual void flush() {
		if (!st_enabled)return;
		if (p_soundtouch) {
			p_soundtouch->clear();

		}
		m_rate = 0;
		m_ch = 0;
		m_ch_mask = 0;
	}

	virtual double get_latency() {
		if (!st_enabled) return 0;
		if (p_soundtouch)
		{
			return p_soundtouch->numSamples() / (double)m_rate;
		}
		return 0;
	}


	virtual bool need_track_change_mark() {
		return false;
	}

	static bool g_get_default_preset(dsp_preset & p_out)
	{
		make_preset(0.0, false, p_out);
		return true;
	}
	static void g_show_config_popup(const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback)
	{
		::RunDSPConfigPopup(p_data, p_parent, p_callback);
	}
	static bool g_have_config_popup() { return true; }
	static void make_preset(float pitch, bool enabled, dsp_preset & out)
	{
		dsp_preset_builder builder;
		builder << pitch;
		builder << enabled;
		builder.finish(g_get_guid(), out);
	}
	static void parse_preset(float & pitch, bool &enabled, const dsp_preset & in)
	{
		try
		{
			dsp_preset_parser parser(in);
			parser >> pitch;
			parser >> enabled;
		}
		catch (exception_io_data) { pitch = 0.0; enabled = false; }
	}
};

class dsp_tempo : public dsp_impl_base
{
	SoundTouch * p_soundtouch;
	RubberBandStretcher * rubber;
	float **plugbuf;
	float **m_scratch;
	float* buffer_;
	int m_rate, m_ch, m_ch_mask;
	int pitch_shifter;
	float tempo_amount;
	bool st_enabled;
private:
	void flushchunks()
	{
		if (rubber)
		{
			while (1)
			{
				t_size samples = rubber->available();
				if (!samples)return;
				samples = rubber->retrieve(m_scratch, samples);
				if (samples)
				{

					for (int c = 0; c < m_ch; ++c) {
						int j = 0;
						while (j < samples) {
							buffer_[j * m_ch + c] = m_scratch[c][j];
							++j;
						}
					}
					audio_chunk * chunk = insert_chunk(samples*m_ch);
					chunk->set_data(buffer_, samples, m_ch, m_rate);
				}
			}
		}

	}

public:
	dsp_tempo( dsp_preset const & in ) : tempo_amount(0.00), m_rate( 0 ), m_ch( 0 ), m_ch_mask( 0 )
	{
		p_soundtouch = NULL;
		rubber = NULL;
		plugbuf = NULL;
		m_scratch = NULL;
		st_enabled =false;
		buffer_ = NULL;
		pitch_shifter = 0;
		parse_preset(tempo_amount,pitch_shifter,st_enabled, in);
	}
	~dsp_tempo(){
		if (rubber)
		{
			flushchunks();
			
			for (int c = 0; c < m_ch; ++c)
			{
				delete[] plugbuf[c]; plugbuf[c] = NULL;
				delete[] m_scratch[c]; m_scratch[c] = NULL;
			}
			if (buffer_)
			{
				delete[] buffer_;
				buffer_ = NULL;
			}
			delete[] plugbuf;
			delete[] m_scratch;
			m_scratch = NULL;
			plugbuf = NULL;
			delete rubber;
			rubber = 0;

		}

		if (p_soundtouch)
		{
			p_soundtouch->clear();
			if (buffer_)
			{
				delete[] buffer_;
				buffer_ = NULL;
			}
			delete p_soundtouch;
			p_soundtouch = NULL;
		}
	}

	// Every DSP type is identified by a GUID.
	static GUID g_get_guid() {
		// {44BCACA2-9EDD-493A-BB8F-9474F4B5A76B}
		
		return guid_tempo;
	}

	// We also need a name, so the user can identify the DSP.
	// The name we use here does not describe what the DSP does,
	// so it would be a bad name. We can excuse this, because it
	// doesn't do anything useful anyway.
	static void g_get_name(pfc::string_base & p_out) {
		p_out = "Tempo Shift";
	}

	virtual void on_endoftrack(abort_callback & p_abort) {
		if (rubber && st_enabled)
		{
			flushchunks();
		}
	}

	virtual void on_endofplayback(abort_callback & p_abort) {
		//same as flush, only at end of playback
		if (rubber &&st_enabled)
		{
			flushchunks();
		}
	}

	// The framework feeds input to our DSP using this method.
	// Each chunk contains a number of samples with the same
	// stream characteristics, i.e. same sample rate, channel count
	// and channel configuration.




	virtual bool on_chunk(audio_chunk * chunk, abort_callback & p_abort) {
		t_size sample_count = chunk->get_sample_count();
		audio_sample * src = chunk->get_data();

        if (tempo_amount == 0)
        {
            st_enabled = false;
            return true;
        }
        if (!st_enabled)st_enabled = true;

		if (chunk->get_srate() != m_rate || chunk->get_channels() != m_ch || chunk->get_channel_config() != m_ch_mask)
		{
			m_rate = chunk->get_srate();
			m_ch = chunk->get_channels();
			m_ch_mask = chunk->get_channel_config();

			if (pitch_shifter == 1)
			{
				RubberBandStretcher::Options options = RubberBandStretcher::DefaultOptions | RubberBandStretcher::OptionProcessRealTime | RubberBandStretcher::OptionThreadingNever;
				float ratios = 1. / (1. + (tempo_amount / 100.));
			//	ratios = ((ratios - 0.5) * 1000 );
				if (rubber)
				{
					delete rubber;
					rubber = NULL;

				}
				if (plugbuf) {
					for (int c = 0; c < m_ch; ++c) delete[] plugbuf[c];
					delete[] plugbuf;
				}
				if (m_scratch) {
					for (int c = 0; c < m_ch; ++c) delete[] m_scratch[c];
					delete[] m_scratch;
				}
				if (buffer_)
				{
					delete[] buffer_;
					buffer_ = NULL;
				}
				buffer_ = new float[BUFFER_SIZE_RBTEMPO*m_ch * 64];

				rubber = new RubberBandStretcher(m_rate, m_ch, options, 1.1, 1.0);
            rubber->setTimeRatio(ratios);
				m_scratch = new float *[m_ch];
				plugbuf = new float *[m_ch];
				if (!rubber) return 0;
			//	rubber->setMaxProcessSize(BUFFER_SIZE_RBTEMPO*m_ch);
				for (int c = 0; c < m_ch; ++c) plugbuf[c] = new float[BUFFER_SIZE_RBTEMPO * 64];
				for (int c = 0; c < m_ch; ++c) m_scratch[c] = new float[BUFFER_SIZE_RBTEMPO *64];
				st_enabled = true;
			}
			else if(pitch_shifter == 0)
			{
				if (p_soundtouch)
				{
					delete[] buffer_;
					buffer_ = NULL;
					delete p_soundtouch;
					p_soundtouch = NULL;
				}

				buffer_ = new float[BUFFER_SIZE*m_ch];
				p_soundtouch = new SoundTouch;
				if (!p_soundtouch) return 0;
				if (p_soundtouch)
				{
					p_soundtouch->setSampleRate(m_rate);
					p_soundtouch->setChannels(m_ch);
					p_soundtouch->setTempoChange(tempo_amount);
					bool usequickseek = true;
					bool useaafilter = true; //seems clearer without it
					p_soundtouch->setSetting(SETTING_USE_QUICKSEEK, true);
					p_soundtouch->setSetting(SETTING_USE_AA_FILTER, useaafilter);
				}
			}
			
		}

		if (rubber && pitch_shifter == 1) {
       


			t_size sample_count = chunk->get_sample_count();
		
			audio_sample * current = chunk->get_data();
         int processed = 0;
         size_t outTotal = 0;

         while (processed < sample_count)
         {
            size_t out_samples_gen = rubber->getSamplesRequired();
            int inchunk = min(sample_count - processed, out_samples_gen);

            for (int i = 0; i < inchunk; i++) {
                  for (int j = 0; j <m_ch; j++) {
                     plugbuf[j][i] = *current++;
               }
            }
         
            rubber->process(plugbuf, inchunk, false);
            processed += inchunk;
            if (rubber->available())
            {
               flushchunks();
            }
          
         }
			
			
		}
		else if (p_soundtouch && pitch_shifter == 0)
		{
			t_size sample_count = chunk->get_sample_count();
			size_t out_samples_gen = BUFFER_SIZE;
			audio_sample * current = chunk->get_data();
			p_soundtouch->putSamples(current, sample_count);
			while (out_samples_gen > 0)
			{
				out_samples_gen = p_soundtouch->receiveSamples(buffer_, out_samples_gen);
				if (out_samples_gen != 0)
				{
					audio_chunk * chunk = insert_chunk(out_samples_gen * m_ch);
					chunk->set_data(buffer_, out_samples_gen, m_ch, m_rate, m_ch_mask);
				}
			}
		}

		return false;
	}


	virtual void flush() {
		if (!st_enabled)return;

		if (p_soundtouch) {
			p_soundtouch->clear();

		}
		if (rubber)
		{
			flushchunks();
		}
		m_rate = 0;
		m_ch = 0;
		m_ch_mask = 0;
	}

	virtual double get_latency() {
		if (!st_enabled ) return 0;
		if (rubber && pitch_shifter == 1){
			return (double)(rubber->available()*m_ch)/m_rate;
		}
		if (p_soundtouch && pitch_shifter == 0)
		{
			return p_soundtouch->numSamples() / (double)m_rate;
		}
		return 0;
	}


	virtual bool need_track_change_mark() {
		return false;
	}

	static bool g_get_default_preset(dsp_preset & p_out)
	{
		make_preset(0.0,0,false, p_out);
		return true;
	}
	static void g_show_config_popup( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback )
	{
		::RunDSPConfigPopupTempo( p_data, p_parent, p_callback );
	}
	static bool g_have_config_popup() { return true; }
	static void make_preset(float pitch,int pitch_shifter,bool enabled, dsp_preset & out)
	{
		dsp_preset_builder builder;
		builder << pitch;
		builder << pitch_shifter;
		builder << enabled;
		builder.finish(g_get_guid(), out);
	}
	static void parse_preset(float & pitch,int & pitch_shifter,bool & enabled, const dsp_preset & in)
	{
		try
		{
			dsp_preset_parser parser(in);
			parser >> pitch;
			parser >> pitch_shifter;
			parser >> enabled;
		}
		catch (exception_io_data) { pitch = 0.0; pitch_shifter = 0; enabled = false; }
	}
};

class dsp_rate : public dsp_impl_base
{
	SoundTouch * p_soundtouch;
	int m_rate, m_ch, m_ch_mask;
	float pitch_amount;
	float* buffer_;
	bool st_enabled;

public:
	dsp_rate(dsp_preset const & in) : pitch_amount(0.00), m_rate(0), m_ch(0), m_ch_mask(0)
	{
		p_soundtouch = 0;
		buffer_ = NULL;
		st_enabled = false;
		parse_preset(pitch_amount, st_enabled, in);

	}
	~dsp_rate() {
		if (p_soundtouch)
		{
			p_soundtouch->clear();
			delete[] buffer_;
			buffer_ = NULL;
			delete p_soundtouch;
			p_soundtouch = 0;
		}

	}

	// Every DSP type is identified by a GUID.
	static GUID g_get_guid() {
		// {8C12D81E-BB88-4056-B4C0-EAFA4E9F3B95}

		return guid_pbrate;
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

	}

	// The framework feeds input to our DSP using this method.
	// Each chunk contains a number of samples with the same
	// stream characteristics, i.e. same sample rate, channel count
	// and channel configuration.
	virtual bool on_chunk(audio_chunk * chunk, abort_callback & p_abort) {


        if (pitch_amount == 0.0)
        {
            st_enabled = false;
            return true;
        }
        if (!st_enabled) st_enabled = true;


		if (chunk->get_srate() != m_rate || chunk->get_channels() != m_ch || chunk->get_channel_config() != m_ch_mask)
		{
			if (p_soundtouch)delete p_soundtouch;
			if (buffer_)delete[] buffer_;


			m_rate = chunk->get_srate();
			m_ch = chunk->get_channels();
			m_ch_mask = chunk->get_channel_config();
			p_soundtouch = new SoundTouch;
			if (!p_soundtouch) return 0;
			p_soundtouch->setSampleRate(m_rate);
			p_soundtouch->setChannels(m_ch);
			p_soundtouch->setRateChange(pitch_amount);
			st_enabled = true;
			buffer_ = new float[BUFFER_SIZE*m_ch];

		}
		if (p_soundtouch && st_enabled)
		{
			t_size sample_count = chunk->get_sample_count();
			size_t out_samples_gen = BUFFER_SIZE;
			audio_sample * current = chunk->get_data();
			p_soundtouch->putSamples(current, sample_count);
			while (out_samples_gen > 0)
			{
				out_samples_gen = p_soundtouch->receiveSamples(buffer_, out_samples_gen);
				if (out_samples_gen != 0)
				{
					audio_chunk * chunk = insert_chunk(out_samples_gen * m_ch);
					chunk->set_data(buffer_, out_samples_gen, m_ch, m_rate, m_ch_mask);
				}
			}
		}
		return false;
	}

	virtual void flush() {
		if (!st_enabled)return;
		if (p_soundtouch) {
			p_soundtouch->clear();

		}
		m_rate = 0;
		m_ch = 0;
		m_ch_mask = 0;
	}

	virtual double get_latency() {
		if (!st_enabled) return 0;
		if (p_soundtouch)
		{
			return p_soundtouch->numSamples() / (double)m_rate;
		}
		return 0;
	}

	virtual bool need_track_change_mark() {
		return false;
	}

	static bool g_get_default_preset(dsp_preset & p_out)
	{
		make_preset(0.0, false, p_out);
		return true;
	}
	static void g_show_config_popup(const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback)
	{
		::RunDSPConfigPopupRate(p_data, p_parent, p_callback);
	}
	static bool g_have_config_popup() { return true; }
	static void make_preset(float pitch, bool enabled, dsp_preset & out)
	{
		dsp_preset_builder builder;
		builder << pitch;
		builder << enabled;
		builder.finish(g_get_guid(), out);
	}
	static void parse_preset(float & pitch, bool & enabled, const dsp_preset & in)
	{
		try
		{
			dsp_preset_parser parser(in);
			parser >> pitch;
			parser >> enabled;
		}
		catch (exception_io_data) { pitch = 0.0; enabled = false; }
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
		pitchmax = 2400
		
	};
	BEGIN_MSG_MAP( CMyDSPPopup )
		MSG_WM_INITDIALOG( OnInitDialog )
		COMMAND_HANDLER_EX( IDOK, BN_CLICKED, OnOKButton )
		COMMAND_HANDLER_EX( IDCANCEL, BN_CLICKED, OnButton )
        COMMAND_HANDLER(IDC_RESET, BN_CLICKED, OnReset)
		COMMAND_HANDLER_EX(IDC_PITCHTYPE, CBN_SELCHANGE, OnChange)
        MESSAGE_HANDLER(WM_USER, OnEditControlChange)
		MSG_WM_HSCROLL( OnScroll )
	END_MSG_MAP()
private:
	void DSPConfigChange(dsp_chain_config const & cfg)
	{
		if (m_hWnd != NULL) {
			ApplySettings();
		}
	}

	void ApplySettings()
	{
		dsp_preset_impl preset2;
		if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_pitch, preset2)) {
			float  pitch;
			bool enabled;
			dsp_pitch::parse_preset(pitch,enabled, preset2);
			float pitch_val = pitch *= 100.00;
			slider_drytime.SetPos((double)(pitch_val + 1200));
			RefreshLabel(pitch_val / 100.00);
		}
	}

    LRESULT OnReset(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
    {
        float pitch = 0;
        float pitch3 = (pitch * 100) + 1200;
        slider_drytime.SetPos((double)(pitch3));
        
        bool enabled = false;
        dsp_preset_impl preset;
        enabled = true;
        dsp_pitch::make_preset(pitch, enabled, preset);
        m_callback.on_preset_changed(preset);
        RefreshLabel(pitch);


        return 0;
    }

	BOOL OnInitDialog(CWindow, LPARAM)
	{
		slider_drytime = GetDlgItem(IDC_PITCH);
		slider_drytime.SetRange(0, pitchmax);
        pitch_edit.AttachToDlgItem(m_hWnd);
        pitch_edit.SubclassWindow(GetDlgItem(IDC_PITCH_EDIT));
        CWindow w = GetDlgItem(IDC_PITCHALGO);
        CWindow wind(m_hWnd);
        wind.SetWindowTextW(L"Pitch Shift DSP Configuration");
        w.ShowWindow(SW_HIDE);
        w = GetDlgItem(IDC_TEMPOTYPE);
        w.ShowWindow(SW_HIDE);
		int pitch_type;
		{
			float  pitch;
			bool enabled = true;
			dsp_pitch::parse_preset(pitch,enabled, m_initData);
			pitch *= 100.00;
			slider_drytime.SetPos( (double)(pitch+1200));
			RefreshLabel( pitch/100.00);
		}
		return TRUE;
	}


    void GetEditText()
    {
        CString sWindowText;
        pitch_edit.GetWindowText(sWindowText);
        float pitch2 = _ttof(sWindowText);
        pitch2 = clamp_ml(pitch2, 12.0, -12.0);
        if (pitch_s != sWindowText)
        {

            float pitch3 = pitch2 * 100.00;
            slider_drytime.SetPos((double)(pitch3 + 1200));
            dsp_preset_impl preset;
            dsp_pitch::make_preset(pitch2, true, preset);
            m_callback.on_preset_changed(preset);
        }
    }
    void OnOKButton(UINT, int id, CWindow)
    {
        GetEditText();
        EndDialog(id);
    }

	void OnButton( UINT, int id, CWindow )
	{
		EndDialog( id );
	}

	void OnChange(UINT scrollID, int id, CWindow window)
	{
			float pitch;
			pitch = slider_drytime.GetPos() - 1200;
			pitch /= 100.00;

			bool enabled = false;
			{
				dsp_preset_impl preset;
				enabled = true;
				dsp_pitch::make_preset(pitch,enabled, preset);
				m_callback.on_preset_changed(preset);
			}
			RefreshLabel(pitch);
	}

	void OnScroll(UINT scrollID, int id, CWindow window)
	{
		float pitch;
		pitch = slider_drytime.GetPos() - 1200;
		pitch /= 100.00;

		bool enabled = false;
		if ((LOWORD(scrollID) != SB_THUMBTRACK) && window.m_hWnd == slider_drytime.m_hWnd)
		{
            bool enabled = false;
			dsp_preset_impl preset;
			enabled = true;
			dsp_pitch::make_preset(pitch, enabled, preset);
			m_callback.on_preset_changed(preset);
		}
		RefreshLabel(pitch);
	}

    LRESULT OnEditControlChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        if (wParam == 0x1988)
        {
            GetEditText();
        }
        return 0;
    }

    void RefreshLabel(float  pitch)
    {
        pfc::string_formatter msg;
        msg << "Pitch: ";
        msg << (pitch < 0 ? "" : "+");
        ::uSetDlgItemText(*this, IDC_PITCHINFOS1, msg);
        msg.reset();
        msg << pfc::format_float(pitch, 0, 2);
        CString sWindowText;
        sWindowText = msg.c_str();
        pitch_s = sWindowText;
        pitch_edit.SetWindowText(sWindowText);
        msg.reset();
        msg << "semitones";
        ::uSetDlgItemText(*this, IDC_PITCHINFOS2, msg);

    }


	const dsp_preset & m_initData; // modal dialog so we can reference this caller-owned object.
	dsp_preset_edit_callback & m_callback;
    CEditMod pitch_edit;
    CString pitch_s;
	CTrackBarCtrl slider_drytime,slider_wettime,slider_dampness,slider_roomwidth,slider_roomsize;
};

static void RunDSPConfigPopup(const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback)
{
	CMyDSPPopupPitch popup(p_data, p_callback);

	if (popup.DoModal(p_parent) != IDOK) p_callback.on_preset_changed(p_data);
}

class CMyDSPPopupRate : public CDialogImpl<CMyDSPPopupRate>
{
public:
	CMyDSPPopupRate( const dsp_preset & initData, dsp_preset_edit_callback & callback ) : m_initData( initData ), m_callback( callback ) { }
	enum { IDD = IDD_PITCH };
	enum
	{
		pitchmin = 0,
		pitchmax = 15000

	};
	BEGIN_MSG_MAP( CMyDSPPopup )
		MSG_WM_INITDIALOG( OnInitDialog )
		COMMAND_HANDLER_EX( IDOK, BN_CLICKED, OnOKButton )
		COMMAND_HANDLER_EX( IDCANCEL, BN_CLICKED, OnButton )
		MSG_WM_HSCROLL( OnHScroll )
        MESSAGE_HANDLER(WM_USER, OnEditControlChange)
        COMMAND_HANDLER(IDC_RESET, BN_CLICKED, OnReset);
	END_MSG_MAP()
private:
	void DSPConfigChange(dsp_chain_config const & cfg)
	{
		if (m_hWnd != NULL) {
			ApplySettings();
		}
	}


	void ApplySettings()
	{
		dsp_preset_impl preset2;
		if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_pbrate, preset2)) {
			float  rate;
			bool enabled;
			int pitch_type;
			dsp_rate::parse_preset(rate,enabled, preset2);
			slider_drytime.SetPos((double)(rate+5000));
			RefreshLabel(rate);
		}
	}

	BOOL OnInitDialog(CWindow, LPARAM)
	{
		slider_drytime = GetDlgItem(IDC_PITCH);
		slider_drytime.SetRange(0, pitchmax);
        pitch_edit.AttachToDlgItem(m_hWnd);
        pitch_edit.SubclassWindow(GetDlgItem(IDC_PITCH_EDIT));
        CWindow w = GetDlgItem(IDC_PITCHALGO);
        CWindow wind(m_hWnd);
        wind.SetWindowTextW(L"Playback Rate Shift DSP Configuration");
        w.ShowWindow(SW_HIDE);
        w = GetDlgItem(IDC_TEMPOTYPE);
        w.ShowWindow(SW_HIDE);

		{
			float  pitch;
			bool enabled;
			dsp_rate::parse_preset(pitch,enabled, m_initData);
            pitch *= 100;
			slider_drytime.SetPos( (double)(pitch+5000));
			RefreshLabel( pitch/100);
		}
		
		return TRUE;
	}

	void OnButton( UINT, int id, CWindow )
	{
		EndDialog( id );
	}

    void OnOKButton(UINT, int id, CWindow)
    {
        GetEditText();
        EndDialog(id);
    }

	void OnHScroll( UINT nSBCode, UINT nPos, CWindow window )
	{
		float pitch;
		pitch = slider_drytime.GetPos()-5000;
        pitch /= 100;
		{
			if ((LOWORD(nSBCode) != SB_THUMBTRACK) && window.m_hWnd == slider_drytime.m_hWnd)
			{
				dsp_preset_impl preset;
              
				dsp_rate::make_preset(pitch, true, preset);
				m_callback.on_preset_changed(preset);
			}

		}
		RefreshLabel( pitch);
	}

    void RefreshLabel(float  pitch)
    {
        pfc::string_formatter msg;
        msg << "PB Rate: ";
        msg << (pitch < 0 ? "" : "+");
        ::uSetDlgItemText(*this, IDC_PITCHINFOS1, msg);
        msg.reset();
        msg << pfc::format_float(pitch, 0, 2);
        CString sWindowText;
        sWindowText = msg.c_str();
        pitch_s = sWindowText;
        pitch_edit.SetWindowText(sWindowText);
        msg.reset();
        msg << "%";
        ::uSetDlgItemText(*this, IDC_PITCHINFOS2, msg);

    }

    void GetEditText()
    {
        CString sWindowText;
        pitch_edit.GetWindowText(sWindowText);
        float pitch2 = _ttof(sWindowText);
        pitch2 = clamp_ml(pitch2, 100.0, -50.0);
        //  pitch2 = round(pitch2);
        if (pitch_s != sWindowText)
        {

            float pitch3 = (pitch2 * 100) + 5000;
            slider_drytime.SetPos((double)(pitch3));

            dsp_preset_impl preset;
            dsp_rate::make_preset(pitch2, true, preset);
            m_callback.on_preset_changed(preset);
        }
    }

    LRESULT OnEditControlChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        if (wParam == 0x1988)
        {
            GetEditText();
        }
        return 0;
    }

    LRESULT OnReset(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
    {
        float pitch = 0;
        float pitch3 = (pitch * 100) + 5000;
        slider_drytime.SetPos((double)(pitch3));
        dsp_preset_impl preset;

        dsp_rate::make_preset(pitch, true, preset);
        m_callback.on_preset_changed(preset);
        RefreshLabel(0);
        return 0;
    }

	const dsp_preset & m_initData; // modal dialog so we can reference this caller-owned object.
	dsp_preset_edit_callback & m_callback;
    CEditMod pitch_edit;
    CString pitch_s;
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
	CMyDSPPopupTempo(const dsp_preset & initData, dsp_preset_edit_callback & callback) : m_initData(initData), m_callback(callback) { }
	enum { IDD = IDD_PITCH };
	enum
	{
		pitchmin = 0,
		tempomax = 19000

	};
	BEGIN_MSG_MAP(CMyDSPPopup)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDOK, BN_CLICKED, OnOKButton)
		COMMAND_HANDLER_EX(IDCANCEL, BN_CLICKED, OnButton)
		COMMAND_HANDLER_EX(IDC_TEMPOTYPE, CBN_SELCHANGE, OnChange)
        MESSAGE_HANDLER(WM_USER, OnEditControlChange)
        
        COMMAND_HANDLER(IDC_RESET, BN_CLICKED, OnReset);
        MSG_WM_HSCROLL(OnScroll)
	END_MSG_MAP()
private:

	BOOL OnInitDialog(CWindow, LPARAM)
	{
		slider_drytime = GetDlgItem(IDC_PITCH);
		slider_drytime.SetRange(0,tempomax);
        pitch_edit.AttachToDlgItem(m_hWnd);
        pitch_edit.SubclassWindow(GetDlgItem(IDC_PITCH_EDIT));
        CWindow wind(m_hWnd);
        wind.SetWindowTextW(L"Tempo Shift DSP Configuration");
		CWindow w = GetDlgItem(IDC_TEMPOTYPE);
		uSendMessageText(w, CB_ADDSTRING, 0, "SoundTouch");
		uSendMessageText(w, CB_ADDSTRING, 0, "Rubber Band");
		int pitch_type;
		{
			float  pitch;
			bool enabled;
			dsp_tempo::parse_preset(pitch, pitch_type, enabled, m_initData);
			::SendMessage(w, CB_SETCURSEL, pitch_type, 0);
			float tempo2 = pitch * 100;
			slider_drytime.SetPos((double)(tempo2 + 9500));
			RefreshLabel(pitch);
		}
		return TRUE;
	}

    void GetEditText()
    {
        CString sWindowText;
        pitch_edit.GetWindowText(sWindowText);
        float pitch2 = _ttof(sWindowText);
        pitch2 = clamp_ml(pitch2, 95.0, -95.0);
        if (pitch_s != sWindowText)
        {
            float pitch3 = pitch2 * 100.00;
            slider_drytime.SetPos((double)(pitch3 + 9500));
            int p_type; //filter type
            p_type = SendDlgItemMessage(IDC_TEMPOTYPE, CB_GETCURSEL);
            {
                dsp_preset_impl preset;
                dsp_tempo::make_preset(pitch2, p_type, true, preset);
                m_callback.on_preset_changed(preset);
            }
        }
    }

    LRESULT OnEditControlChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        if (wParam == 0x1988)
        {
            GetEditText();
        }
        return 0;
    }


    LRESULT OnReset(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
    {
        float pitch = 0;
        float pitch3 = (pitch * 100) + 9500;
        slider_drytime.SetPos((double)(pitch3));
        int p_type; //filter type
        p_type = SendDlgItemMessage(IDC_TEMPOTYPE, CB_GETCURSEL);
        {
            dsp_preset_impl preset;
            dsp_tempo::make_preset(pitch, p_type, true, preset);
            m_callback.on_preset_changed(preset);
        }
        RefreshLabel(pitch);
        return 0;
    }

    void OnOKButton(UINT, int id, CWindow)
    {
        GetEditText();
        EndDialog(id);
    }

	void OnButton(UINT, int id, CWindow)
	{
		EndDialog(id);
	}

	void OnChange(UINT, int id, CWindow)
	{
		float pitch;
		float tempos = slider_drytime.GetPos() - 9500;
		pitch = tempos / 100.;
		int p_type; //filter type
		p_type = SendDlgItemMessage(IDC_TEMPOTYPE, CB_GETCURSEL);
		{
			dsp_preset_impl preset;
			dsp_tempo::make_preset(pitch, p_type, true, preset);
			m_callback.on_preset_changed(preset);
		}
		RefreshLabel(pitch);
	}

	void OnScroll(UINT scrollID, int id, CWindow window)
	{
		float pitch;
		float tempos = slider_drytime.GetPos() - 9500;
		pitch = tempos / 100.;
		int p_type; //filter type
		p_type = SendDlgItemMessage(IDC_TEMPOTYPE, CB_GETCURSEL);
		if ((LOWORD(scrollID) != SB_THUMBTRACK) && window.m_hWnd == slider_drytime.m_hWnd)
		{
			dsp_preset_impl preset;
			dsp_tempo::make_preset(pitch, p_type, true, preset);
			m_callback.on_preset_changed(preset);
		}
		RefreshLabel(pitch);
	}


    void RefreshLabel(float  pitch)
    {
        pfc::string_formatter msg;
        msg << "Tempo: ";
        msg << (pitch < 0 ? "" : "+");
        ::uSetDlgItemText(*this, IDC_PITCHINFOS1, msg);
        msg.reset();
        msg << pfc::format_float(pitch, 0, 2);
        CString sWindowText;
        sWindowText = msg.c_str();
        pitch_s = sWindowText;
        pitch_edit.SetWindowText(sWindowText);
        msg.reset();
        msg << "%";
        ::uSetDlgItemText(*this, IDC_PITCHINFOS2, msg);

    }
	const dsp_preset & m_initData; // modal dialog so we can reference this caller-owned object.
	dsp_preset_edit_callback & m_callback;
	CTrackBarCtrl slider_drytime;
    CString pitch_s;
    CEditMod pitch_edit;
};
static void RunDSPConfigPopupTempo(const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback)
{
	CMyDSPPopupTempo popup(p_data, p_callback);
	if (popup.DoModal(p_parent) != IDOK) p_callback.on_preset_changed(p_data);
}



// {7448B6AD-7FBE-4F72-9F4F-A740620A9C38}
static const GUID guid_pitchelement =
{ 0x7448b6ad, 0x7fbe, 0x4f72,{ 0x9f, 0x4f, 0xa7, 0x40, 0x62, 0xa, 0x9c, 0x38 } };




class uielem_pitch : public CDialogImpl<uielem_pitch> , public ui_element_instance {
public:
   uielem_pitch(ui_element_config::ptr cfg, ui_element_instance_callback::ptr cb) : m_callback(cb) {
      pitch = 0.0; pitch_enabled = false;
   }
   enum { IDD = IDD_PITCH_ELEMENT };
   enum
   {
      pitchmin = 0,
      pitchmax = 2400,

   };
   BEGIN_MSG_MAP_EX(uielem_pitch)
       MSG_WM_INITDIALOG(OnInitDialog)
       COMMAND_HANDLER_EX(IDC_PITCHENABLED_UI, BN_CLICKED, OnEnabledToggle)
       MESSAGE_HANDLER(WM_USER, OnEditControlChange)
       COMMAND_HANDLER(IDC_RESET,BN_CLICKED, OnReset);
      MSG_WM_HSCROLL(OnScroll)
   END_MSG_MAP()



   void initialize_window(HWND parent) { WIN32_OP(Create(parent) != NULL);
   }
   HWND get_wnd() { return m_hWnd; }
   void set_configuration(ui_element_config::ptr config) {
      shit = parseConfig(config);
      if (m_hWnd != NULL) {
         ApplySettings();
      }
      m_callback->on_min_max_info_change();
   }
   ui_element_config::ptr get_configuration() { return makeConfig(); }
   static GUID g_get_guid() {
      return guid_pitchelement;
   }
   static void g_get_name(pfc::string_base & out) { out = "Pitch"; }
   static ui_element_config::ptr g_get_default_configuration() {
      return makeConfig(true);
   }
   static const char * g_get_description() { return "Changes the pitch of the current track."; }
   static GUID g_get_subclass() {
      return ui_element_subclass_dsp;
   }

   ui_element_min_max_info get_min_max_info() {
      ui_element_min_max_info ret;

      // Note that we play nicely with separate horizontal & vertical DPI.
      // Such configurations have not been ever seen in circulation, but nothing stops us from supporting such.
      CSize DPI = QueryScreenDPIEx(*this);

      if (DPI.cx <= 0 || DPI.cy <= 0) { // sanity
         DPI = CSize(96, 96);
      }


      ret.m_min_width = MulDiv(450, DPI.cx, 96);
      ret.m_min_height = MulDiv(80, DPI.cy, 96);
      ret.m_max_width = MulDiv(450, DPI.cx, 96);
      ret.m_max_height = MulDiv(80, DPI.cy, 96);

      // Deal with WS_EX_STATICEDGE and alike that we might have picked from host
      ret.adjustForWindow(*this);

      return ret;
   }
private:
   


   void SetPitchEnabled(bool state) { m_buttonPitchEnabled.SetCheck(state ? BST_CHECKED : BST_UNCHECKED); }
   bool IsPitchEnabled() { return m_buttonPitchEnabled == NULL || m_buttonPitchEnabled.GetCheck() == BST_CHECKED; }

   void PitchDisable() {
      static_api_ptr_t<dsp_config_manager>()->core_disable_dsp(guid_pitch);
   }

  
   void PitchEnable(float pitch, bool enabled) {
      dsp_preset_impl preset;
      dsp_pitch::make_preset(pitch, enabled, preset);
      static_api_ptr_t<dsp_config_manager>()->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
   }

   void OnEnabledToggle(UINT uNotifyCode, int nID, CWindow wndCtl) {
      pfc::vartoggle_t<bool> ownUpdate(m_ownPitchUpdate, true);
      if (IsPitchEnabled()) {
         GetConfig();
         dsp_preset_impl preset;
         dsp_pitch::make_preset(pitch, pitch_enabled, preset);
         //yes change api;
         static_api_ptr_t<dsp_config_manager>()->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
      }
      else {
         static_api_ptr_t<dsp_config_manager>()->core_disable_dsp(guid_pitch);
      }

   }
   LRESULT OnEditControlChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
   {
      if (wParam == 0x1988)
      {
         CString sWindowText;
         pitch_edit.GetWindowText(sWindowText);
         float pitch2 = _ttof(sWindowText);
         pitch2 = clamp_ml(pitch2, 12.0, -12.0);
         if (pitch_s != sWindowText)
         {
            pitch = pitch2;
            float pitch3 = pitch * 100.00;
            slider_pitch.SetPos((double)(pitch3 + 1200));
            OnConfigChanged();
         }
      }
      return 0;
   }

   LRESULT OnReset(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
   {
       pitch = 0;
       float pitch3 = (pitch * 100) + 1200;
       slider_pitch.SetPos((double)(pitch3));
       SetConfig();
       OnConfigChanged();
       return 0;
   }

   void OnScroll(UINT scrollID, int pos, CWindow window)
   {
      pfc::vartoggle_t<bool> ownUpdate(m_ownPitchUpdate, true);
      GetConfig();
      if (IsPitchEnabled())
      {

         if ((LOWORD(scrollID) != SB_THUMBTRACK) && window.m_hWnd == slider_pitch.m_hWnd)
         {
            PitchEnable(pitch, pitch_enabled);
         }
      }
   }

   void OnChange(UINT, int id, CWindow)
   {
      pfc::vartoggle_t<bool> ownUpdate(m_ownPitchUpdate, true);
      GetConfig();
      if (IsPitchEnabled())
      {

         OnConfigChanged();
      }
   }

   //set settings if from another control
   void ApplySettings()
   {
      dsp_preset_impl preset;
      if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_pitch, preset)) {
         SetPitchEnabled(true);
         dsp_pitch::parse_preset(pitch, pitch_enabled, preset);
         SetPitchEnabled(pitch_enabled);
         SetConfig();
      }
      else {
         SetPitchEnabled(false);
         SetConfig();
      }
   }

   void OnConfigChanged() {
      if (IsPitchEnabled()) {
         PitchEnable(pitch, pitch_enabled);
      }
      else {
         PitchDisable();
      }
   }


   void GetConfig()
   {
      float pitch_sl = slider_pitch.GetPos() - 1200;
      pitch = pitch_sl / 100.00;
      pitch_enabled = IsPitchEnabled();

      RefreshLabel(pitch);


   }

   void SetConfig()
   {
      float pitch2 = pitch * 100.00;
      slider_pitch.SetPos((double)(pitch2 + 1200));

      RefreshLabel(pitch2 / 100);

   }

   void RefreshLabel(float  pitch)
   {
      pfc::string_formatter msg;
      msg << "Pitch: ";
      msg << (pitch < 0 ? "" : "+");
      ::uSetDlgItemText(*this, IDC_PITCHINFO_UI, msg);
      msg.reset();
      msg << pfc::format_float(pitch, 0, 2);
      CString sWindowText;
      sWindowText = msg.c_str();
      pitch_s = sWindowText;
      pitch_edit.SetWindowText(sWindowText);
      msg.reset();
      msg << "semitones";
      ::uSetDlgItemText(*this, IDC_PITCHINFO_UI2, msg);

   }

   static uint32_t parseConfig(ui_element_config::ptr cfg) {
      ::ui_element_config_parser in(cfg);
      uint32_t flags; in >> flags;
      return 0;

   }
   static ui_element_config::ptr makeConfig(bool init = false) {
      ui_element_config_builder out;

      if (init)
      {
         uint32_t crap = 1;
         out << crap;
      }
      else
      {
         uint32_t crap = 2;
         out << crap;
      }
      return out.finish(g_get_guid());
   }

   BOOL OnInitDialog(CWindow hwnd, LPARAM) {
      pitch_edit.AttachToDlgItem(m_hWnd);
      pitch_edit.SubclassWindow(GetDlgItem(IDC_PITCH_EDIT));
      slider_pitch = GetDlgItem(IDC_PITCH_UI);
      m_buttonPitchEnabled = GetDlgItem(IDC_PITCHENABLED_UI);
      slider_pitch.SetRange(0, pitchmax);
      CWindow w = GetDlgItem(IDC_TEMPOTYPE_UI);
      w.ShowWindow(SW_HIDE);
      w = GetDlgItem(IDC_TEMPOALG);
      w.ShowWindow(SW_HIDE);
      m_ownPitchUpdate = false;
      ApplySettings();
      return FALSE;
   }


   uint32_t shit;


   float  pitch;
   bool pitch_enabled;
   CTrackBarCtrl slider_pitch;
   CButton m_buttonPitchEnabled;
   CEditMod pitch_edit;
   CString pitch_s;
   bool m_ownPitchUpdate;
protected:
   const ui_element_instance_callback::ptr m_callback;
};

class myElem2 : public  ui_element_impl_withpopup< uielem_pitch > {
   bool get_element_group(pfc::string_base & p_out)
   {
      p_out = "Effect DSP";
      return true;
   }
   bool get_menu_command_description(pfc::string_base & out) {
      out = "Opens a window for pitch control.";
      return true;
   }
};
static service_factory_single_t<myElem2> g_myElemFactory2;

// {C9C585C0-FC4F-42A2-B475-8176041DDC4C}
static const GUID guid_tempoui
{ 0xc9c585c0, 0xfc4f, 0x42a2,{ 0xb4, 0x75, 0x81, 0x76, 0x4, 0x1d, 0xdc, 0x4c } };


class uielem_tempo : public CDialogImpl<uielem_tempo>, public ui_element_instance {
public:
   uielem_tempo(ui_element_config::ptr cfg, ui_element_instance_callback::ptr cb) : m_callback(cb) {
      pitch = 0.0; pitch_enabled = false; t_type = 0;
   }
   enum { IDD = IDD_PITCH_ELEMENT };
   enum
   {
      pitchmin = 0,
      pitchmax = 19000,

   };
   BEGIN_MSG_MAP_EX(uielem_tempo)
      MSG_WM_INITDIALOG(OnInitDialog)
      COMMAND_HANDLER_EX(IDC_PITCHENABLED_UI, BN_CLICKED, OnEnabledToggle)
      MESSAGE_HANDLER(WM_USER, OnEditControlChange)
      MSG_WM_HSCROLL(OnScroll)
      COMMAND_HANDLER(IDC_RESET, BN_CLICKED, OnReset);
      END_MSG_MAP()



   void initialize_window(HWND parent) {
      WIN32_OP(Create(parent) != NULL);
   }
   HWND get_wnd() { return m_hWnd; }
   void set_configuration(ui_element_config::ptr config) {
      shit = parseConfig(config);
      if (m_hWnd != NULL) {
         ApplySettings();
      }
      m_callback->on_min_max_info_change();
   }
   ui_element_config::ptr get_configuration() { return makeConfig(); }
   static GUID g_get_guid() {
      return guid_tempoui;
   }
   static void g_get_name(pfc::string_base & out) { out = "Tempo"; }
   static ui_element_config::ptr g_get_default_configuration() {
      return makeConfig(true);
   }
   static const char * g_get_description() { return "Changes the tempo of the current track."; }
   static GUID g_get_subclass() {
      return ui_element_subclass_dsp;
   }

   ui_element_min_max_info get_min_max_info() {
      ui_element_min_max_info ret;

      // Note that we play nicely with separate horizontal & vertical DPI.
      // Such configurations have not been ever seen in circulation, but nothing stops us from supporting such.
      CSize DPI = QueryScreenDPIEx(*this);

      if (DPI.cx <= 0 || DPI.cy <= 0) { // sanity
         DPI = CSize(96, 96);
      }


      ret.m_min_width = MulDiv(450, DPI.cx, 96);
      ret.m_min_height = MulDiv(120, DPI.cy, 96);
      ret.m_max_width = MulDiv(450, DPI.cx, 96);
      ret.m_max_height = MulDiv(120, DPI.cy, 96);

      // Deal with WS_EX_STATICEDGE and alike that we might have picked from host
      ret.adjustForWindow(*this);

      return ret;
   }
private:



   void SetPitchEnabled(bool state) { m_buttonPitchEnabled.SetCheck(state ? BST_CHECKED : BST_UNCHECKED); }
   bool IsPitchEnabled() { return m_buttonPitchEnabled == NULL || m_buttonPitchEnabled.GetCheck() == BST_CHECKED; }

   void PitchDisable() {
      static_api_ptr_t<dsp_config_manager>()->core_disable_dsp(guid_tempo);
   }

   double clamp(double x, double upper, double lower)
   {
      return min(upper, max(x, lower));
   }
   void PitchEnable(float pitch, bool enabled) {
      dsp_preset_impl preset;
      dsp_tempo::make_preset(pitch,t_type, enabled, preset);
      static_api_ptr_t<dsp_config_manager>()->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
   }

   void OnEnabledToggle(UINT uNotifyCode, int nID, CWindow wndCtl) {
      pfc::vartoggle_t<bool> ownUpdate(m_ownPitchUpdate, true);
      if (IsPitchEnabled()) {
         GetConfig();
         dsp_preset_impl preset;
         dsp_tempo::make_preset(pitch,t_type, pitch_enabled, preset);
         //yes change api;
         static_api_ptr_t<dsp_config_manager>()->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
      }
      else {
         static_api_ptr_t<dsp_config_manager>()->core_disable_dsp(guid_tempo);
      }

   }
   LRESULT OnEditControlChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
   {
      if (wParam == 0x1988)
      {
         CString sWindowText;
         pitch_edit.GetWindowText(sWindowText);
         float pitch2 = _ttof(sWindowText);
         pitch2 = clamp(pitch2, 95.0, -95.0);
         if (pitch_s != sWindowText)
         {
            pitch = pitch2;
            float pitch3 = pitch * 100.00;
            t_type = SendDlgItemMessage(IDC_TEMPOTYPE_UI, CB_GETCURSEL);
            slider_pitch.SetPos((double)(pitch3 + 9500));
            OnConfigChanged();
         }
      }
      return 0;
   }

   LRESULT OnReset(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
   {
       pitch = 0;
       float pitch3 = (0 * 100) + 9500;
       slider_pitch.SetPos((double)(pitch3));
       SetConfig();
       OnConfigChanged();
       return 0;
   }

   void OnScroll(UINT scrollID, int pos, CWindow window)
   {
      pfc::vartoggle_t<bool> ownUpdate(m_ownPitchUpdate, true);
      GetConfig();
      if (IsPitchEnabled())
      {

         if ((LOWORD(scrollID) != SB_THUMBTRACK) && window.m_hWnd == slider_pitch.m_hWnd)
         {
            PitchEnable(pitch, pitch_enabled);
         }
      }
   }

   void OnChange(UINT, int id, CWindow)
   {
      pfc::vartoggle_t<bool> ownUpdate(m_ownPitchUpdate, true);
      GetConfig();
      if (IsPitchEnabled())
      {

         OnConfigChanged();
      }
   }

   //set settings if from another control
   void ApplySettings()
   {
      dsp_preset_impl preset;
      if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_tempo, preset)) {
         SetPitchEnabled(true);
         dsp_tempo::parse_preset(pitch,t_type, pitch_enabled, preset);
         SetPitchEnabled(pitch_enabled);
         SetConfig();
      }
      else {
         SetPitchEnabled(false);
         SetConfig();
      }
   }

   void OnConfigChanged() {
      if (IsPitchEnabled()) {
         PitchEnable(pitch, pitch_enabled);
      }
      else {
         PitchDisable();
      }
   }


   void GetConfig()
   {
      float tempos = slider_pitch.GetPos() - 9500;
      pitch = tempos / 100.;
      t_type = SendDlgItemMessage(IDC_TEMPOTYPE_UI, CB_GETCURSEL);
      pitch_enabled = IsPitchEnabled();

      RefreshLabel(pitch);


   }

   void SetConfig()
   {
      float tempo2 = pitch * 100;
      CWindow w = GetDlgItem(IDC_TEMPOTYPE_UI);
      ::SendMessage(w, CB_SETCURSEL, t_type, 0);
      slider_pitch.SetPos((double)(tempo2 + 9500));

      RefreshLabel(tempo2 / 100);

   }

   void RefreshLabel(float  pitch)
   {
      pfc::string_formatter msg;
      msg << "Tempo: ";
      msg << (pitch < 0 ? "" : "+");
      ::uSetDlgItemText(*this, IDC_PITCHINFO_UI, msg);
      msg.reset();
      msg << pfc::format_float(pitch, 0, 2);
      CString sWindowText;
      sWindowText = msg.c_str();
      pitch_s = sWindowText;
      pitch_edit.SetWindowText(sWindowText);
      msg.reset();
      msg << "%";
      ::uSetDlgItemText(*this, IDC_PITCHINFO_UI2, msg);

   }

   static uint32_t parseConfig(ui_element_config::ptr cfg) {
      ::ui_element_config_parser in(cfg);
      uint32_t flags; in >> flags;
      return 0;

   }
   static ui_element_config::ptr makeConfig(bool init = false) {
      ui_element_config_builder out;

      if (init)
      {
         uint32_t crap = 1;
         out << crap;
      }
      else
      {
         uint32_t crap = 2;
         out << crap;
      }
      return out.finish(g_get_guid());
   }

   BOOL OnInitDialog(CWindow hwnd, LPARAM) {
      pitch_edit.AttachToDlgItem(m_hWnd);
      pitch_edit.SubclassWindow(GetDlgItem(IDC_PITCH_EDIT));
      slider_pitch = GetDlgItem(IDC_PITCH_UI);
      m_buttonPitchEnabled = GetDlgItem(IDC_PITCHENABLED_UI);
      slider_pitch.SetRange(0, pitchmax);
      CWindow w = GetDlgItem(IDC_TEMPOTYPE_UI);
      w.ShowWindow(SW_SHOW);
      uSendMessageText(w, CB_ADDSTRING, 0, "SoundTouch");
      uSendMessageText(w, CB_ADDSTRING, 0, "Rubber Band");
      w = GetDlgItem(IDC_TEMPOALG);
      w.ShowWindow(SW_SHOW);
      m_ownPitchUpdate = false;
      ApplySettings();
      return FALSE;
   }


   uint32_t shit;


   float  pitch;
   int t_type;
   bool pitch_enabled;
   CTrackBarCtrl slider_pitch;
   CButton m_buttonPitchEnabled;
   CEditMod pitch_edit;
   CString pitch_s;
   bool m_ownPitchUpdate;
protected:
   const ui_element_instance_callback::ptr m_callback;
};

class myElem3 : public  ui_element_impl_withpopup< uielem_tempo > {
   bool get_element_group(pfc::string_base & p_out)
   {
      p_out = "Effect DSP";
      return true;
   }
   bool get_menu_command_description(pfc::string_base & out) {
      out = "Opens a window for tempo control.";
      return true;
   }
};
static service_factory_single_t<myElem3> g_myElemFactory3;

// {8D49FEB4-FC5A-4D63-961D-5D86AA8610F7}
static const GUID guid_rateui =
{ 0x8d49feb4, 0xfc5a, 0x4d63,{ 0x96, 0x1d, 0x5d, 0x86, 0xaa, 0x86, 0x10, 0xf7 } };


class uielem_rate : public CDialogImpl<uielem_rate>, public ui_element_instance {
public:
   uielem_rate(ui_element_config::ptr cfg, ui_element_instance_callback::ptr cb) : m_callback(cb) {
      pitch = 0.0; pitch_enabled = false;
   }
   enum { IDD = IDD_PITCH_ELEMENT };
   enum
   {
   };
   BEGIN_MSG_MAP_EX(uielem_rate)
      MSG_WM_INITDIALOG(OnInitDialog)
      COMMAND_HANDLER_EX(IDC_PITCHENABLED_UI, BN_CLICKED, OnEnabledToggle)
      MESSAGE_HANDLER(WM_USER, OnEditControlChange)
       COMMAND_HANDLER(IDC_RESET, BN_CLICKED, OnReset);
      MSG_WM_HSCROLL(OnScroll)
      END_MSG_MAP()



   void initialize_window(HWND parent) {
      WIN32_OP(Create(parent) != NULL);
   }
   HWND get_wnd() { return m_hWnd; }
   void set_configuration(ui_element_config::ptr config) {
      shit = parseConfig(config);
      if (m_hWnd != NULL) {
         ApplySettings();
      }
      m_callback->on_min_max_info_change();
   }
   ui_element_config::ptr get_configuration() { return makeConfig(); }
   static GUID g_get_guid() {
      return guid_rateui;
   }
   static void g_get_name(pfc::string_base & out) { out = "Playback Rate"; }
   static ui_element_config::ptr g_get_default_configuration() {
      return makeConfig(true);
   }
   static const char * g_get_description() { return "Changes the playback rate of the current track."; }
   static GUID g_get_subclass() {
      return ui_element_subclass_dsp;
   }

   ui_element_min_max_info get_min_max_info() {
      ui_element_min_max_info ret;

      // Note that we play nicely with separate horizontal & vertical DPI.
      // Such configurations have not been ever seen in circulation, but nothing stops us from supporting such.
      CSize DPI = QueryScreenDPIEx(*this);

      if (DPI.cx <= 0 || DPI.cy <= 0) { // sanity
         DPI = CSize(96, 96);
      }


      ret.m_min_width = MulDiv(450, DPI.cx, 96);
      ret.m_min_height = MulDiv(80, DPI.cy, 96);
      ret.m_max_width = MulDiv(450, DPI.cx, 96);
      ret.m_max_height = MulDiv(80, DPI.cy, 96);

      // Deal with WS_EX_STATICEDGE and alike that we might have picked from host
      ret.adjustForWindow(*this);

      return ret;
   }


private:



   void SetPitchEnabled(bool state) { m_buttonPitchEnabled.SetCheck(state ? BST_CHECKED : BST_UNCHECKED); }
   bool IsPitchEnabled() { return m_buttonPitchEnabled == NULL || m_buttonPitchEnabled.GetCheck() == BST_CHECKED; }

   void PitchDisable() {
      static_api_ptr_t<dsp_config_manager>()->core_disable_dsp(guid_pbrate);
   }

   double clamp(double x, double upper, double lower)
   {
      return min(upper, max(x, lower));
   }
   void PitchEnable(float pitch, bool enabled) {
      dsp_preset_impl preset;
      dsp_rate::make_preset(pitch, enabled, preset);
      static_api_ptr_t<dsp_config_manager>()->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
   }

   void OnEnabledToggle(UINT uNotifyCode, int nID, CWindow wndCtl) {
      pfc::vartoggle_t<bool> ownUpdate(m_ownPitchUpdate, true);
      if (IsPitchEnabled()) {
         GetConfig();
         dsp_preset_impl preset;
         dsp_rate::make_preset(pitch, pitch_enabled, preset);
         //yes change api;
         static_api_ptr_t<dsp_config_manager>()->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
      }
      else {
         static_api_ptr_t<dsp_config_manager>()->core_disable_dsp(guid_pbrate);
      }

   }
   LRESULT OnEditControlChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
   {
      if (wParam == 0x1988)
      {
         CString sWindowText;
         pitch_edit.GetWindowText(sWindowText);
         float pitch2 = _ttof(sWindowText);
         pitch2 = clamp(pitch2, 100.0, -50.0);
       //  pitch2 = round(pitch2);
         if (pitch_s != sWindowText)
         {
            pitch = pitch2;
            float pitch3 = (pitch*100) + 5000;
            slider_pitch.SetPos((double)(pitch3));
            OnConfigChanged();
         }
      }
      return 0;
   }

   LRESULT OnReset(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
   {
       pitch = 0;
       float pitch3 = (pitch * 100) + 5000;
       slider_pitch.SetPos((double)(pitch3));
       SetConfig();
       OnConfigChanged();
       return 0;
   }

   void OnScroll(UINT scrollID, int pos, CWindow window)
   {
      pfc::vartoggle_t<bool> ownUpdate(m_ownPitchUpdate, true);
      GetConfig();
      if (IsPitchEnabled())
      {

         if ((LOWORD(scrollID) != SB_THUMBTRACK) && window.m_hWnd == slider_pitch.m_hWnd)
         {
            PitchEnable(pitch, pitch_enabled);
         }
      }
   }

   void OnChange(UINT, int id, CWindow)
   {
      pfc::vartoggle_t<bool> ownUpdate(m_ownPitchUpdate, true);
      GetConfig();
      if (IsPitchEnabled())
      {

         OnConfigChanged();
      }
   }

   //set settings if from another control
   void ApplySettings()
   {
      dsp_preset_impl preset;
      if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_pbrate, preset)) {
         SetPitchEnabled(true);
         dsp_rate::parse_preset(pitch, pitch_enabled, preset);
         SetPitchEnabled(pitch_enabled);
         SetConfig();
      }
      else {
         SetPitchEnabled(false);
         SetConfig();
      }
   }

   void OnConfigChanged() {
      if (IsPitchEnabled()) {
         PitchEnable(pitch, pitch_enabled);
      }
      else {
         PitchDisable();
      }
   }


   void GetConfig()
   {
      float pitch_sl = slider_pitch.GetPos() - 5000;
      pitch = pitch_sl/100;
      pitch_enabled = IsPitchEnabled();

      RefreshLabel(pitch);


   }

   void SetConfig()
   {
      slider_pitch.SetPos((double)((pitch*100) + 5000));
      RefreshLabel(pitch);

   }

   void RefreshLabel(float  pitch)
   {
      pfc::string_formatter msg;
      msg << "PB Rate: ";
      msg << (pitch < 0 ? "" : "+");
      ::uSetDlgItemText(*this, IDC_PITCHINFO_UI, msg);
      msg.reset();
      msg << pfc::format_float(pitch, 0, 2);
      CString sWindowText;
      sWindowText = msg.c_str();
      pitch_s = sWindowText;
      pitch_edit.SetWindowText(sWindowText);
      msg.reset();
      msg << "%";
      ::uSetDlgItemText(*this, IDC_PITCHINFO_UI2, msg);

   }

   static uint32_t parseConfig(ui_element_config::ptr cfg) {
      ::ui_element_config_parser in(cfg);
      uint32_t flags; in >> flags;
      return 0;

   }
   static ui_element_config::ptr makeConfig(bool init = false) {
      ui_element_config_builder out;

      if (init)
      {
         uint32_t crap = 1;
         out << crap;
      }
      else
      {
         uint32_t crap = 2;
         out << crap;
      }
      return out.finish(g_get_guid());
   }

 
   BOOL OnInitDialog(CWindow hwnd, LPARAM) {
      pitch_edit.AttachToDlgItem(m_hWnd);
      pitch_edit.SubclassWindow(GetDlgItem(IDC_PITCH_EDIT));
      slider_pitch = GetDlgItem(IDC_PITCH_UI);
      m_buttonPitchEnabled = GetDlgItem(IDC_PITCHENABLED_UI);
      slider_pitch.SetRange(0, 15000);
      CWindow w = GetDlgItem(IDC_TEMPOTYPE_UI);
      w.ShowWindow(SW_HIDE);
      w = GetDlgItem(IDC_TEMPOALG);
      w.ShowWindow(SW_HIDE);
      m_ownPitchUpdate = false;
      ApplySettings();
      return FALSE;
   }


   uint32_t shit;


   float  pitch;
   bool pitch_enabled;
   CTrackBarCtrl slider_pitch;
   CButton m_buttonPitchEnabled;
   CEditMod pitch_edit;
   CString pitch_s;
   bool m_ownPitchUpdate;
protected:
   const ui_element_instance_callback::ptr m_callback;
};

class myElem : public  ui_element_impl_withpopup< uielem_rate > {
   bool get_element_group(pfc::string_base & p_out)
   {
      p_out = "Effect DSP";
      return true;
   }
   bool get_menu_command_description(pfc::string_base & out) {
      out = "Opens a window for playback rate control.";
      return true;
   }
};
static service_factory_single_t<myElem> g_myElemFactory;

static dsp_factory_t<dsp_tempo> g_dsp_tempo_factory;
static dsp_factory_t<dsp_pitch> g_dsp_pitch_factory;
static dsp_factory_t<dsp_rate> g_dsp_rate_factory;