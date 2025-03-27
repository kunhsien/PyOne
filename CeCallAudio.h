#ifndef CECALLAUDIO_H_
#define CECALLAUDIO_H_
#include "Poco/Logger.h"
#include <unordered_map>
#include "../../AudioManager/include/CAudioFunction.h"
#include "CLogger.h"

namespace MD {
namespace eCallMgr {

class eCallManager;
class CeCallAudio
{
public:
    CeCallAudio(eCallManager* const pMgr);
    virtual ~CeCallAudio();

	uint8_t get_ivs_language(); // << [LE0-18125] 20250114 Tracy >>
    virtual void finish_audio_testtone(); // << [LE0-6682][LE0-7190] 20230816 CK >>
    virtual bool play_audio_with_language(const std::__cxx11::string data, const bool isUninterruptAudio = true); // << [LE0-7069][LE0-6210][LE0-5724][LE0-5671] 20230621 CK >>
    virtual void set_audio_device(std::__cxx11::string device);
	virtual void set_audio_param_path(std::__cxx11::string path);
    void play_audio_with_testtone(const uint16_t frequency, const uint8_t volume); // << [LE0-6682][LE0-7190] 20230816 CK >>
    // << [LE0-6062] 20230630 CK
    void cleanNeedPlay();
    void cleanCurrentPlay009(); // << [LE0-7069] 20230904 CK >>
    void cleanCurrentPlay008(); // << [LE0-6210] 20230710 CK >>
    void cleanCurrentPlay003(); // << [LE0-6210] 20230710 CK >>
    void handleNeedPlay();
    void playAudioWithLanguage008(); // << [LE0-6210] 20230710 CK >>
    void playAudioWithLanguage009(); // << [LE0-7069] 20230904 CK >>
    void playAudioWithLanguage003();
    void playAudioWithLanguage004();
    // >> [LE0-6062]

    bool isCurrentOnPlay() const {return m_isCurOnPlay;}
    void setCurrentOnPlay(const bool value){m_isCurOnPlay = value;}

	static uint8_t getDefaultLang() {return 1U;} // << [LE0-14696] 20240627 LalaYHTseng >>
    void removeDataAudioFile(); // << [LE0-18125] 20250314 EasonCCLiao >>
public:	
	typedef enum
	{
		GENERIC = -1,
		fr_FR = 0,
		en_GB = 1,
		de_DE = 2,
		es_ES = 3,
		it_IT = 4,
		pt_PT = 5,
		nl_NL = 6,
		el_GR = 7,
		pt_BR = 8, // << [LE0-18125] 20250304 EasonCCLiao >>
		pl_PL = 9,
		zh_CN = 11,
		tr_TR = 12,
		ru_RU = 14,
		cs_CZ = 15,
		hr_HR = 16,
		hu_HU = 17,
		ar_SA = 18,
		da_DK = 21, // << [LE0-6094] 20230705 CK >>
		fa_IR = 23,
		fi_FI = 24,
		he_IL = 25,
		no_NO = 26, // << [LE0-6094] 20230705 CK >>
		ro_RO = 27,
		sv_SE = 29,
		sk_SK = 36,
		sl_SI = 37,

	}audioLang;
    
private:
	std::__cxx11::string getFilePath(const std::__cxx11::string data) const;
	std::__cxx11::string getLangCode()const;
	bool isLanguageSupported(uint8_t langCode); // << [LE0-18125] 20250114 Tracy  >>
    eCallManager* const pecallMgr;
    CLogger& _logger;
        
    // << [LE0-6062] 20230630 CK
    bool mIsNeedPlay003{false};
    bool mIsNeedPlay004{false};
    // >> [LE0-6062]
    bool mIsCurOnPlay009{false}; // << [LE0-7069] 20230904 CK >>
    bool mIsCurOnPlay008{false}; // << [LE0-6210] 20230710 CK >>
    bool mIsCurOnPlay003{false}; // << [LE0-6210] 20230710 CK >>
    
    bool m_isCurOnPlay{false};
    std::string m_curFile;
    MD::AudioMgr::CAudioFunction::TestToneInfo mTestToneInfo; // << [LE0-6682][LE0-7190] 20230816 CK >>

	const std::unordered_map<audioLang,std::__cxx11::string> LangCode = {
		{ audioLang::ar_SA , "ar_SA" },
		{ audioLang::cs_CZ , "cs_CZ" },
		{ audioLang::da_DK , "da_DK" },
		{ audioLang::de_DE , "de_DE" },
		{ audioLang::el_GR , "el_GR" },
		{ audioLang::en_GB , "en_GB" },
		{ audioLang::es_ES , "es_ES" },
		{ audioLang::fi_FI , "fi_FI" },
		{ audioLang::fr_FR , "fr_FR" },
		{ audioLang::he_IL , "he_IL" },
		{ audioLang::hr_HR , "hr_HR" },
		{ audioLang::hu_HU , "hu_HU" },
		{ audioLang::it_IT , "it_IT" },
		{ audioLang::nl_NL , "nl_NL" },
		{ audioLang::no_NO , "no_NO" },
		{ audioLang::pl_PL , "pl_PL" },
		{ audioLang::pt_PT , "pt_PT" },
		{ audioLang::ro_RO , "ro_RO" },
		{ audioLang::ru_RU , "ru_RU" },
		{ audioLang::sk_SK , "sk_SK" },
		{ audioLang::sl_SI , "sl_SI" },
		{ audioLang::sv_SE , "sv_SE" },
		{ audioLang::tr_TR , "tr_TR" },
		{ audioLang::zh_CN , "zh_CN" },
		{ audioLang::fa_IR , "fa_IR" },
		{ audioLang::pt_BR , "pt_BR" }, // << [LE0-18125] 20250304 EasonCCLiao >>
		{ audioLang::GENERIC , "00_GENERIC" }
	};

	const std::unordered_map<uint8_t, audioLang> ivsLangMap = {
		{ 18U, audioLang::ar_SA },
		{ 15U, audioLang::cs_CZ },
		{ 21U, audioLang::da_DK },
		{ 2U,  audioLang::de_DE },
		{ 7U,  audioLang::el_GR },
		{ 1U,  audioLang::en_GB },
		{ 3U,  audioLang::es_ES },
		{ 24U, audioLang::fi_FI },
		{ 0U,  audioLang::fr_FR },
		{ 25U, audioLang::he_IL },
		{ 16U, audioLang::hr_HR },
		{ 17U, audioLang::hu_HU },
		{ 4U,  audioLang::it_IT },
		{ 6U,  audioLang::nl_NL },
		{ 26U, audioLang::no_NO },
		{ 9U,  audioLang::pl_PL },
		{ 5U,  audioLang::pt_PT },
		{ 27U, audioLang::ro_RO },
		{ 14U, audioLang::ru_RU },
		{ 36U, audioLang::sk_SK },
		{ 37U, audioLang::sl_SI },
		{ 29U, audioLang::sv_SE },
		{ 12U, audioLang::tr_TR },
		{ 11U, audioLang::zh_CN },
		{ 23U, audioLang::fa_IR },
		{ 8U, audioLang::pt_BR } // << [LE0-18125] 20250304 EasonCCLiao >>
	};

	const std::string AUDIO_PROMPT_PATH{"/usr/local/xCall/xCalI_vocal_prompts/"}; // << [LE0-18125] 20250313 MaoJunYan >>
    const std::string AUDIO_PROMPT_FILENAME {"/xCall_Audio_Prompt_"};
    const std::string USING_WAV {".wav"};

};

} } // namespace MD::eCallMgr
#endif