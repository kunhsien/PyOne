#include "CeCallAudio.h"
#include "eCallManager.h"
#include "fmt/core.h"
#include "CanFrame_temp.h"
#include "CANFrameTable.h"
#include "DataStore.h"
#include "CeCallMachine.h"
#include "CeCallCmd.h"
#include "CeCallXmlConfig.h"
#include "Poco/File.h"
#include "Poco/Thread.h"
using Poco::Thread;
using MD::AudioMgr::CAudioFunction;
using MD::eCallMgr::CeCallAudio;

namespace MD {
namespace eCallMgr {

CeCallAudio::CeCallAudio(eCallManager* const pMgr)
	:pecallMgr(pMgr)
    ,_logger(CLogger::newInstance("Aud"))
{
    
}
CeCallAudio::~CeCallAudio()
{
  
}

uint8_t CeCallAudio::get_ivs_language() // << [LE0-18125] 20250114 Tracy >>
{
    bool ret = false;
	canDcRawFrameData_t cf2{0U, 0x0U, 8U, { 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U } };
    canDcRawFrameData_t cf3{0U, 0x0U, 8U, { 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U } };
	ret = pecallMgr->pDataStore()->getCANFrame( static_cast<uint16_t>(Frame_BSI_INF_PROFILS_2), cf2 );
    const uint8_t originalLang = pecallMgr->peCallMachine()->getInternalIVSLanguage(); // << [LE0-10901][LE0-6235] 20230711 CK: REQ-0314816 >>
    uint8_t lang = originalLang;
	if(ret)
	{
		uint8_t DISPO_UNITES_LANGUE = static_cast<uint8_t>(pecallMgr->pDataStore()->parseCANData(cf2.payload, 19, 1));
        // << [LE0-11930] TracyTu, get different can by different can arch
		uint8_t LANGUE_VHL_2 = 0; 
        uint16_t canArch = pecallMgr->pDataStore()->getCanArch();
        _logger.notice("%s", fmt::format("get_ivs_language canArch:{}", canArch));       
        if(canArch == 1U )
        {
            ret = pecallMgr->pDataStore()->getCANFrame( static_cast<uint16_t>(Frame_DONNEES_BSI_LENTES_4), cf3 );
            if(ret)
            {
                LANGUE_VHL_2  = static_cast<uint8_t>(pecallMgr->pDataStore()->parseCANData(cf3.payload, 27, 6));
                _logger.notice("%s", fmt::format("From 2F6 LANGUE_VHL_2:{} ",LANGUE_VHL_2));     
            }
            else
            {
                _logger.error("%s", fmt::format("get LANGUE_VHL_2 from Frame_DONNEES_BSI_LENTES_4 fail, ret:{}", ret));
            }
     
            if(LANGUE_VHL_2 == 0)
            {
                LANGUE_VHL_2 = static_cast<uint8_t>(pecallMgr->pDataStore()->parseCANData(cf2.payload, 31, 6));  
                _logger.notice("%s", fmt::format("From 220 LANGUE_VHL_2:{} ",LANGUE_VHL_2));                       
            }            
        }
        else
        {
            LANGUE_VHL_2 = static_cast<uint8_t>(pecallMgr->pDataStore()->parseCANData(cf2.payload, 31, 6));  
        }
        _logger.warning("%s", fmt::format("DISPO_UNITES_LANGUE:{} LANGUE_VHL_2:{} ",DISPO_UNITES_LANGUE, LANGUE_VHL_2));
        // >> [LE0-11930] 
        // << [LE0-18125] 20250114 Tracy 
        lang = (DISPO_UNITES_LANGUE == 1U && isLanguageSupported(LANGUE_VHL_2)) ? LANGUE_VHL_2 : CeCallAudio::getDefaultLang();
        if (lang != originalLang) {
            pecallMgr->peCallMachine()->setInternalIVSLanguage(lang); // << [LE0-10901][LE0-6235] 20230711 CK >>
            pecallMgr->peCallXmlConfig()->saveIVSLanguage(lang); // << [LE0-10901] 20240223 ck >>  
        }
        // >> [LE0-18125] 
	}
	else
	{
	      _logger.error("%s", fmt::format("get_ivs_language Frame_BSI_INF_PROFILS_2 fail ret:{}",ret));
	}

    return lang; // << [LE0-18125] 20250114 Tracy >>
}

// << [LE0-18125] 20250114 Tracy 
bool CeCallAudio::isLanguageSupported(uint8_t langCode) {
    bool result = ivsLangMap.find(langCode) != ivsLangMap.end();
    _logger.notice("%s", fmt::format("langCode:{}, isLanguageSupported:{}", langCode, result));
    return result;
}
// >> [LE0-18125] 

// << [LE0-6682][LE0-7190] 20230816 CK
void CeCallAudio::finish_audio_testtone() {
    _logger.information("%s", fmt::format("finish_audio_testtone ecallstate:{}", pecallMgr->peCallMachine()->getEcallMachineState()));
    if((eCallState::ECALL_CALL != pecallMgr->peCallMachine()->getEcallMachineState()) && (eCallState::ECALL_VOICE_COMMUNICATION != pecallMgr->peCallMachine()->getEcallMachineState()) && (eCallState::ECALL_MSD_TRANSFER != pecallMgr->peCallMachine()->getEcallMachineState())) {    
        pecallMgr->pCmdThread()->sendstringcmd(AUDIOMANAGER_TYPE, AUDIO_GPIO_MUTE, static_cast<uint16_t>(NORMAL), std::string(""));	
    }
    m_isCurOnPlay = false;
}
void CeCallAudio::play_audio_with_testtone(const uint16_t frequency, const uint8_t volume)
{
    _logger.information("%s", fmt::format("play_audio_with_testtone m_isCurOnPlay:{}", m_isCurOnPlay));
    // << [LE0-5724][LE0-5671] 20230621 CK
    _logger.information("%s", fmt::format("play_audio_with_testtone ecallstate:{}", pecallMgr->peCallMachine()->getEcallMachineState()));
    if ((eCallState::ECALL_CALL != pecallMgr->peCallMachine()->getEcallMachineState()) && (eCallState::ECALL_VOICE_COMMUNICATION != pecallMgr->peCallMachine()->getEcallMachineState()) && (eCallState::ECALL_MSD_TRANSFER != pecallMgr->peCallMachine()->getEcallMachineState())) {
        pecallMgr->pCmdThread()->sendstringcmd(AUDIOMANAGER_TYPE, AUDIO_GPIO_UNMUTE, static_cast<uint16_t>(NORMAL), std::string(""));
    }
    m_isCurOnPlay = true;
    mTestToneInfo.setFrequency(frequency);
    mTestToneInfo.setVolume(volume);
    pecallMgr->pCmdThread()->sendanycmd(AUDIOMANAGER_TYPE, AUDIO_WAV_START_TEST_TONE_PLAY, static_cast<uint16_t>(NORMAL), mTestToneInfo);
    // >> [LE0-5724][LE0-5671]
}
// [LE0-6682][LE0-7190] 20230816 CK >>
// << [LE0-6062] 20230630 CK
void CeCallAudio::cleanNeedPlay() {
    mIsNeedPlay003 = false;
    mIsNeedPlay004 = false;
    cleanCurrentPlay009(); // << [LE0-7069] 20230904 CK >>
    cleanCurrentPlay008(); // << [LE0-6210] 20230710 CK >>
    cleanCurrentPlay003(); // << [LE0-6210] 20230710 CK >>
}
// << [LE0-6210] 20230710 CK
// << [LE0-7069] 20230904 CK
void CeCallAudio::cleanCurrentPlay009() {
    mIsCurOnPlay009 = false;
}
// >> [LE0-7069]
void CeCallAudio::cleanCurrentPlay008() {
    mIsCurOnPlay008 = false;
}
void CeCallAudio::cleanCurrentPlay003() {
    mIsCurOnPlay003 = false;
}
// >> [LE0-6210]
void CeCallAudio::handleNeedPlay() {
    if (mIsNeedPlay003){
        playAudioWithLanguage003();
    } else if(mIsNeedPlay004) {
        playAudioWithLanguage004();
    }
    else{
        _logger.information("no pending prompt!");
    }
}
// << [LE0-6210] 20230710 CK
void CeCallAudio::playAudioWithLanguage008() {
    _logger.notice("%s", fmt::format("playAudioWithLanguage008"));
    if (play_audio_with_language("008")) { // << [LE0-7069] 20230904 CK >>
        mIsCurOnPlay008 = true;
    } else {
        _logger.error(fmt::format("{}: fail", __func__));
    }
}
// >> [LE0-6210]
// << [LE0-7069] 20230904 CK
void CeCallAudio::playAudioWithLanguage009() {
    _logger.notice("%s", fmt::format("playAudioWithLanguage009"));
    if (play_audio_with_language("009")) {
        mIsCurOnPlay009 = true;
    } else {
        _logger.error(fmt::format("{}: fail", __func__));
    }
}
void CeCallAudio::playAudioWithLanguage003() {
    _logger.notice("%s", fmt::format("playAudioWithLanguage003 mIsCurOnPlay009:{} mIsCurOnPlay008:{} mIsNeedPlay003:{}", mIsCurOnPlay009, mIsCurOnPlay008, mIsNeedPlay003));
    if (mIsCurOnPlay009 || mIsCurOnPlay008) { // << [LE0-6210] 20230710 CK >>
        mIsNeedPlay003 = true;
    } else {
        mIsNeedPlay003 = false;
        if (play_audio_with_language("003", false)) {
            mIsCurOnPlay003 = true; // << [LE0-6210] 20230710 CK >>
        } else {
            _logger.error(fmt::format("{}: fail", __func__));
        }
    }
}
void CeCallAudio::playAudioWithLanguage004() {
    _logger.notice("%s", fmt::format("playAudioWithLanguage004 mIsCurOnPlay009:{} mIsCurOnPlay008:{} mIsCurOnPlay003:{} mIsNeedPlay003:{} mIsNeedPlay004:{}", mIsCurOnPlay009, mIsCurOnPlay008, mIsCurOnPlay003, mIsNeedPlay003, mIsNeedPlay004));
    if (mIsCurOnPlay009 || mIsCurOnPlay008 || mIsCurOnPlay003 || mIsNeedPlay003) { // << [LE0-6210] 20230710 CK >>
        mIsNeedPlay004 = true; // << [LE0-6062][LE0-6111][LE0-6082] 20230704 CK >>
    } else {
        mIsNeedPlay004 = false;
        if (!play_audio_with_language("004", false)) {
            _logger.error(fmt::format("{}: fail", __func__));
        }
    }
}
// >> [LE0-7069]
// >> [LE0-6062]
bool CeCallAudio::play_audio_with_language(const std::__cxx11::string data, const bool isUninterruptAudio) // << [LE0-6210][LE0-5724][LE0-5671] 20230621 CK >>
{
    bool ret = false;	
    (void)get_ivs_language(); // << [LE0-18125] 20250114 Tracy >>
	Poco::File audioFile( getFilePath(data) ); 
	
	if(audioFile.exists())
	{
        _logger.information("%s", fmt::format("m_isCurOnPlay:{}", m_isCurOnPlay));
        _logger.notice("%s", fmt::format("[AT] play file:{} ...", audioFile.path().c_str())); // << [LE022-5019] 20240925 EasonCCLiao >>
        // << [LE0-5724][LE0-5671] 20230621 CK
        _logger.notice("%s", fmt::format("[AT] ecallstate:{}", pecallMgr->peCallMachine()->getEcallMachineState())); // << [LE022-5019] 20240925 EasonCCLiao >>
        if ((eCallState::ECALL_CALL != pecallMgr->peCallMachine()->getEcallMachineState()) && (eCallState::ECALL_VOICE_COMMUNICATION != pecallMgr->peCallMachine()->getEcallMachineState()) && (eCallState::ECALL_MSD_TRANSFER != pecallMgr->peCallMachine()->getEcallMachineState())) {
            pecallMgr->pCmdThread()->sendstringcmd(AUDIOMANAGER_TYPE, AUDIO_GPIO_UNMUTE, static_cast<uint16_t>(NORMAL), std::string(""));
        }
        m_curFile = audioFile.path().c_str();
        m_isCurOnPlay = true;

        uint32_t startPlayCmdId = AUDIO_ECALL_START_PLAY;
        if (isUninterruptAudio) {
            startPlayCmdId = AUDIO_ECALL_START_PLAY_UNINTERRUPT;
        }
        pecallMgr->pCmdThread()->sendstringcmd(AUDIOMANAGER_TYPE, startPlayCmdId, static_cast<uint16_t>(NORMAL), audioFile.path().c_str(), 5U);
        // >> [LE0-5724][LE0-5671]
        ret = true; // << [LE0-6210] 20230710 CK >>
    }
	else
	{
 		_logger.fatal("%s", fmt::format("unable to play file, file does not exist...") );
        ret = false; // << [LE0-6210] 20230710 CK >>
	}
    return ret;
}

std::__cxx11::string CeCallAudio::getFilePath(const std::__cxx11::string data) const
{
	std::__cxx11::string path = AUDIO_PROMPT_PATH;

	if( data=="001" || data=="003" || data=="004" ) {
 		path += LangCode.find( audioLang::GENERIC )->second;       
    }
    else {
        // << [LE0-18125] 20250313 EasonCCLiao
        std::__cxx11::string langCode = getLangCode();
        Poco::File file(path + langCode); 
        if(file.exists()) {
            path += langCode;
        } else {
            _logger.error(fmt::format("Language {} not found, defaulting to en_GB", langCode));
            path += "en_GB";
        }
        // >> [LE0-18125]
    }
	path += AUDIO_PROMPT_FILENAME + data + USING_WAV;
	return path;
}

// << [LE0-18125] 20250314 EasonCCLiao
void CeCallAudio::removeDataAudioFile() {
    Poco::File file("/data/MD/etc/xCalI_vocal_prompts"); 
    if(file.exists()) {
        _logger.notice(fmt::format("Removing data audio file: {}", file.path()));
        try {
            file.remove(true);
            _logger.notice(fmt::format("removeDataAudioFile:{}", AUDIO_PROMPT_PATH));
        } catch (Poco::Exception& e) {
            _logger.error(fmt::format("removeDataAudioFile:{}", e.displayText()));
        }
    }
}
// >> [LE0-18125]

std::__cxx11::string CeCallAudio::getLangCode()const
{
    auto defaultlang = ivsLangMap.find(pecallMgr->peCallMachine()->getInternalIVSLanguage());
    if(defaultlang == ivsLangMap.end()){
        defaultlang = ivsLangMap.find(getDefaultLang()); // << [LE0-14696] 20240627 LalaYHTseng >>
    }

	auto langCodePair = LangCode.find(defaultlang->second);
	if( langCodePair == LangCode.end() ) {
		langCodePair = LangCode.find(static_cast<audioLang>(getDefaultLang()));        
    }

	return langCodePair->second;
}

void CeCallAudio::set_audio_device(std::string device)
{
	_logger.notice("%s", fmt::format("set_audio_device:{}", device));
	pecallMgr->pCmdThread()->sendstringcmd( AUDIOMANAGER_TYPE, AUDIO_WAV_DEVICE, static_cast<uint16_t>(NORMAL), device);
}

void CeCallAudio::set_audio_param_path(std::string path)
{
	_logger.notice("%s", fmt::format("set_audio_param_path:{}", path));
	pecallMgr->pCmdThread()->sendstringcmd( AUDIOMANAGER_TYPE, AUDIO_WAV_PARAM_PATH, static_cast<uint16_t>(NORMAL), path);
}

} } // namespace MD::eCallMgr
