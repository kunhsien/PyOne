
#ifndef CECALLMACHINE_H 
#define CECALLMACHINE_H 
#include "Poco/Logger.h"
#include "Poco/Timer.h"
#include "PublicXcall.h"
#include "CeCallState.h"
#include <constant.h>

#include "radiofih/fih_sdk_ecall_api.h"
#include "CeCallFunction.h"
#include "DataStore.h"
#include "CeCallTrace.h" //<< [LE0-10138] TracyTu, eCall trace develop >>
#include "CeCallRecovery.h"  //<< [LE0-14407] TracyTu, eCall recovery >>
#include "CeCallAudio.h" // << [LE0-14696] 20240627 LalaYHTseng >>

namespace MD {
namespace eCallMgr {

class eCallManager;
enum class eCallState
{
	ECALL_OFF,
	ECALL_MANUAL_TRIGGERED,
	ECALL_REGISTRATION,
	ECALL_CALL,
	ECALL_MSD_TRANSFER,
	ECALL_VOICE_COMMUNICATION,
	ECALL_WAITING_FOR_CALLBACK,
	ECALL_WCB_ECALL_IDLE,
	ECALL_WCB_ECALL_MANUAL_TRIGGERED,
};
enum class eCallSubState
{
    ECALL_SUB_NONE,
	ECALL_SUB_FAIL,
	ECALL_SUB_ABNORMAL_HANGUP,
    ECALL_SUB_ACTIVE,
    ECALL_SUB_INCOMING,
    ECALL_SUB_DIAL,
    ECALL_SUB_REDIAL,
    ECALL_SUB_DISCONNECTED,
};
enum class eCallMsdState
{
    ECALL_MSD_NONE,
	ECALL_MSD_SENDING_START,
	ECALL_MSD_SENDING_MSD,
    ECALL_MSD_SENDING_LLACK_RECEIVED,
    ECALL_MSD_SENDING_ALACK_POSITION_RECEIVED,
    ECALL_MSD_SENDING_ALACK_CLEARDOWN_RECEIVED,
    ECALL_MSD_SENDING_TIMEOUT, //<< [LE0-10138] TracyTu, eCall trace develop (phase 5) >>
};
// << [LE0-5958] 20230713 CK
enum class CallPushFlagState
{
    PUSH_FLAG_KEEP,
    PUSH_FLAG_RISE,
    PUSH_FLAG_RESET
};
// >> [LE0-5958]

// << [LE0-13731] 20240514 CK
enum class CallStatus3A9
{
    CALL_OFF = 0,
    ECALL_WCB = 0x10,                   // (bit control) ECall waiting for callback
    ECALL_DIAL = 0x11,                  // (bit control) ECall dial
    ECALL_VOICE_COMMUNICATION = 0x12,   // (bit control) ECall voice communication
    ACALL_WCB = 0x20,                   // (bit control) ACall waiting for callback
    ACALL_DIAL = 0x21,                  // (bit control) ACall dial
    ACALL_VOICE_COMMUNICATION = 0x22,   // (bit control) ACall voice communication
    UNKNOWN = 0x80,
};
// >> [LE0-13731]
//<< [V3-REQ] TracyTu, for xCall status
enum class XcallStatus
{
    NO_XCALL,
    AUTO_ECALL_TRIGGERED,  //Automatic eCall  triggered
    MANUAL_ECALL_TRIGGERED, //Manual eCall triggered
    ACALL_ON,
    FAULT,
};
//>> [V3-REQ] 
//<< [LE0-14407] TracyTu, eCall recovery 
enum genericEvent
{
    PARK_MODE = 0,
    LCM_STATE,
    THERMAL,
};
//>> [LE0-14407]


class CeCallMachine {
private:
    typedef struct ecall_config
    {
    private:
        uint32_t CANCELLATION_TIME{4U};  // 4sec
        uint32_t ECALL_REGISTRATION_TIME{2U}; //2min
        uint32_t ECALL_DIAL_DURATION{5U}; // 5min
        uint32_t MSD_TRANSMISSION_TIME{20U}; // 20sec
        uint32_t CCFT{60U}; // 60min
        uint32_t CALL_AUTO_ANSWER_TIME{60U}; // << [LE0WNC-9451] 20230311 CK: By default : 20 minutes for Ru, 60 min for PE112, 10 min for eCall TPS >>

        std::string ECALL_NUMBER{""}; //<< [REQ-0314811]TracyTu, Adjust TYPE_EMERGENCY_CALL >>
        uint32_t ECALL_DIAL_ATTEMPTS_EU{2U};  //<< [LE0WNC-9450] TracyTu, adjust redial strategy >>
        //uint32_t ECALL_RE_DIAL_TIME{60};
        bool MANUAL_ECALL_ACTIVE{true};
        bool AUTOMATIC_ECALL_ACTIVE{true};
        uint32_t ECALL_T5{5U}; // 5sec
        uint32_t ECALL_T6{5U}; // 5sec
        std::string ECALL_TEST_NUMBER{""};
        uint32_t WAITING_NETWORK_BUB{1U};//1h
        uint32_t WAITING_NETWORK_VB{13U};//13h
        bool ECALL_MANUAL_CAN_CANCEL{true};
        uint32_t ECALL_UNAVAILABLE_REQUEST_TIME{5U};//5sec
        uint32_t ECALL_EVENT_LOGFILE_EXPIRED_TIME{780U};//13*60 min
        // 12hr50min
        uint32_t ECALL_DATA_LOGFILE_EXPIRED_TIME{770U};//12*60 + 50 min // << [LE0-13446] TracyTu, delete MSD data log file >>

        std::string  ECALL_AUDIO_PARAM_PATH{"/system/etc/audio_param"}; // << [LE0WNC-3178] 20230302 CK >> /*<<[LE0WNC-2474] yusanhsu>>*/
        std::string  ECALL_AUDIO_DEVICE{"HANDSFREE"};
        bool ECALL_BUTTON_TRIGGER_DURING_OTA{false};//<<[LE0-4466][REQ-0314919] yusanhsu, Need to retrigger eCall after OTA.>>
        bool ECALL_AUTO_TRIGGER_DURING_OTA{false};//<<[LE0-4466][REQ-0314919] yusanhsu, Need to retrigger eCall after OTA.>>
        uint32_t LATEST_ECALL_TRIGGERED_METHOD_ID{0U}; // << [LE0-3217][RTBMVAL-506] 20230515 CK >>
        int64_t LATEST_ECALL_START_AT{0}; // << [LE0-3217][RTBMVAL-506] 20230515 CK >>
    
    public:
        uint32_t getECallConfigCancelationTime()const {return CANCELLATION_TIME;}
        uint32_t getECallConfigRegistrationTime()const {return ECALL_REGISTRATION_TIME;}
        uint32_t getECallConfigDialDurationTime()const {return ECALL_DIAL_DURATION;}
        uint32_t getECallConfigMsdTransmissionTime()const {return MSD_TRANSMISSION_TIME;}
        uint32_t getECallConfigCCFT()const {return CCFT;}
        uint32_t getECallConfigAutoAnswerTime()const {return CALL_AUTO_ANSWER_TIME;}
        std::string getECallConfigEcallNumber()const {return ECALL_NUMBER;}
        uint32_t getECallConfigDialAttempsEU()const {return ECALL_DIAL_ATTEMPTS_EU;}
        bool getECallConfigMaunalEcallActive()const {return MANUAL_ECALL_ACTIVE;}
        bool getECallConfigAutoEcallActive()const {return AUTOMATIC_ECALL_ACTIVE;}
        uint32_t getECallConfigT5()const {return ECALL_T5;}
        uint32_t getECallConfigT6()const {return ECALL_T6;}
        std::string getECallConfigEcallTestNumber()const {return ECALL_TEST_NUMBER;}
        uint32_t getECallConfigWaitingNetworkBub()const {return WAITING_NETWORK_BUB;}
        uint32_t getECallConfigWaitingNetworkVB()const {return WAITING_NETWORK_VB;}
        bool getECallConfigEcallManualCanCancel()const {return ECALL_MANUAL_CAN_CANCEL;}
        uint32_t getECallConfigUnavailableRequestTime()const {return ECALL_UNAVAILABLE_REQUEST_TIME;}
        uint32_t getECallConfigEventLogFileExpiredTime()const {return ECALL_EVENT_LOGFILE_EXPIRED_TIME;}
        std::string getECallConfigAudioParamPath()const {return ECALL_AUDIO_PARAM_PATH;}
        std::string getECallConfigAudioDevice()const {return ECALL_AUDIO_DEVICE;}
        bool getECallConfigEcallButtonTriggerDuringOTA()const {return ECALL_BUTTON_TRIGGER_DURING_OTA;}
        bool getECallConfigECallAutoTriggerDuringOTA()const {return ECALL_AUTO_TRIGGER_DURING_OTA;}
        uint32_t getECallConfigLatestEcallTriggeredMethodId()const {return LATEST_ECALL_TRIGGERED_METHOD_ID;}
        int64_t getECallConfigLatestEcallStartAt()const {return LATEST_ECALL_START_AT;}
        uint32_t getECallConfigDataLogFileExpiredTime()const {return ECALL_DATA_LOGFILE_EXPIRED_TIME;} // << [LE0-13446] TracyTu, delete MSD data log file >>
        
        
        void setECallConfigCancelationTime(uint32_t const value){CANCELLATION_TIME = value;}
        void setECallConfigRegistrationTime(uint32_t const registrationTime){ECALL_REGISTRATION_TIME = registrationTime;}
        void setECallConfigDialDurationTime(uint32_t const duration){ECALL_DIAL_DURATION = duration;}
        void setECallConfigMsdTransmissionTime(uint32_t const time) {MSD_TRANSMISSION_TIME = time;}
        void setECallConfigCCFT(uint32_t const time){CCFT = time;}
        void setECallConfigAutoAnswerTime(uint32_t const time){CALL_AUTO_ANSWER_TIME = time;}
        void setECallConfigEcallNumber(std::string const number){ECALL_NUMBER = number;}
        void setECallConfigDialAttempsEU(uint32_t const attempts){ECALL_DIAL_ATTEMPTS_EU = attempts;}
        void setECallConfigMaunalEcallActive(bool const active){MANUAL_ECALL_ACTIVE = active;}
        void setECallConfigAutoEcallActive(bool const active){AUTOMATIC_ECALL_ACTIVE = active;}
        void setECallConfigT5(uint32_t const time){ECALL_T5 = time;}
        void setECallConfigT6(uint32_t const time){ECALL_T6 = time;}
        void setECallConfigEcallTestNumber(std::string const number){ECALL_TEST_NUMBER = number;}
        void setECallConfigWaitingNetworkBub(uint32_t const time){WAITING_NETWORK_BUB = time;}
        void setECallConfigWaitingNetworkVB(uint32_t const time){WAITING_NETWORK_VB = time;}
        void setECallConfigEcallManualCanCancel(bool const cancel){ECALL_MANUAL_CAN_CANCEL = cancel;}
        void setECallConfigUnavailableRequestTime(uint32_t const time){ECALL_UNAVAILABLE_REQUEST_TIME = time;}
        void setECallConfigEventLogFileExpiredTime(uint32_t const time){ECALL_EVENT_LOGFILE_EXPIRED_TIME = time;}
        void setECallConfigAudioParamPath(std::string const path){ECALL_AUDIO_PARAM_PATH = path;}
        void setECallConfigAudioDevice(std::string const device){ECALL_AUDIO_DEVICE = device;}
        void setECallConfigEcallButtonTriggerDuringOTA(bool const trigger){ECALL_BUTTON_TRIGGER_DURING_OTA = trigger;}
        void setECallConfigECallAutoTriggerDuringOTA(bool const trigger){ECALL_AUTO_TRIGGER_DURING_OTA = trigger;}
        void setECallConfigLatestEcallTriggeredMethodId(uint32_t const id){LATEST_ECALL_TRIGGERED_METHOD_ID = id;}
        void setECallConfigLatestEcallStartAt(int64_t const time){LATEST_ECALL_START_AT = time;}
        void setECallConfigDataLogFileExpiredTime(uint32_t const time){ECALL_DATA_LOGFILE_EXPIRED_TIME = time;} // << [LE0-13446] TracyTu, delete MSD data log file >>
    }sECallConfig;
    
    typedef struct ecall_internal_data
    {
        // << [LE0-10901] 20240223 ck
        private:
            std::string PARK_MODE{"1"};
            uint32_t LIFE_CYCLE_STATE{static_cast<uint32_t>(LCM_STATE_INACTIVE)};
            std::string THERMAL_MITIGATION{"0"};
            std::string ECALL_VIN_NUMBER{"00000000000000000"}; // << [LE0-9113] 20231019 ck: REQ-0315117 >>
            uint8_t IVS_LANGUAGE{CeCallAudio::getDefaultLang()}; //<< [LE0-9239] TracyTu, ecall only >> << [LE0-14696] 20240627 LalaYHTseng >>
            std::string MODE_CONFIG_VHL{""}; 
        public:
            static const uint32_t LCM_STATE_UNINITIALIZED_VALUE = 0xFFFFU; //<< [LE0-14407] TracyTu, eCall recovery >>
            std::string getParkMode()const {return PARK_MODE;}
            uint32_t getLifeCycleState()const {return LIFE_CYCLE_STATE;}
            std::string getThermalMitigation()const {return THERMAL_MITIGATION;}
            std::string getVINNumber()const {return ECALL_VIN_NUMBER;}
            uint8_t getIVSLanguage()const {return IVS_LANGUAGE;}
            std::string getModeConfigVhl()const {return MODE_CONFIG_VHL;}
            
            void setParkMode(const std::string mode){PARK_MODE = mode;}
            void setLifeCycleState(const uint32_t state){LIFE_CYCLE_STATE = state;}
            void setThermalMitigation(const std::string value){THERMAL_MITIGATION = value;}
            void setVINNumber(const std::string vin){ECALL_VIN_NUMBER = vin;} // << [LE0-7781] 20230901 ck >>
            void setIVSLanguage(const uint8_t language){IVS_LANGUAGE = language;}
            void setModeConfigVhl(const std::string modeConfigVhl){MODE_CONFIG_VHL = modeConfigVhl;}

            //<< [LE0-14407] TracyTu, eCall recovery 
            void resetParkMode(){PARK_MODE = "unknown";} 
            void resetLifeCycleState(){LIFE_CYCLE_STATE = LCM_STATE_UNINITIALIZED_VALUE;}
            void resetThermalMitigation(){THERMAL_MITIGATION = "unknown";}
            //>> [LE0-14407] 
        // >> [LE0-10901]
    }sECallInternalData;

public:
	CeCallMachine(eCallManager* const pMgr);
	virtual ~CeCallMachine();
    virtual void stopTimer();
    void ecall_process_cmd(const CeCallState::Ptr);
    void ecall_process_off(const CeCallState::Ptr );
	void ecall_process_manual_trigger(const CeCallState::Ptr );
    void ecall_process_registration(const CeCallState::Ptr );
    void ecall_process_call(const CeCallState::Ptr );
    void ecall_process_msd_transfer(const CeCallState::Ptr );
	void ecall_process_voice_comm(const CeCallState::Ptr );
    void ecall_process_wait_callback(const CeCallState::Ptr );
	void ecall_trans_state(eCallState nextstate);
    void ecall_manual_cancel_timeout(Poco::Timer&);
	void ecall_registration_timeout(Poco::Timer&);
    void ecall_cellinfo_polling_timeout(Poco::Timer&); // << [LE022-2691] 20240524 ZelosZSCao >>
    void ecall_msd_transfer_timeout(Poco::Timer&);
	void ecall_redial_timeout(Poco::Timer&); //<< [LE0WNC-9450] TracyTu, adjust redial strategy >>
	void ecall_call_timeout(Poco::Timer&);
    void ecall_clear_down_timeout(Poco::Timer&);
    void ecall_beep_prompt_timeout(Poco::Timer &); //<< [LE0-6399] TracyTu, Play beep after pressing the button for two seconds >>
	void ecall_ringtone_prompt_timeout(Poco::Timer&);
	void ecall_registration_prompt_timeout(Poco::Timer&);
	void ecall_callback_prompt_timeout(Poco::Timer&);
    void ecall_callback_msd_log_expired(Poco::Timer&); // << [LE0WNC-3009] 20230207 ZelosZSCao >>
    void ecallDelayGetSignalStrength(Poco::Timer&); // << [LE0-10138] 20231226 CK >>
	void ecall_call_urc_status(const uint32_t state);
	void ecall_comm_urc_status(const uint32_t state, const int32_t callid);
	void ecall_msd_urc_status(const uint32_t state);
	bool ecall_get_cellular_network_srv_state(std::any obj)const;
	uint32_t ecall_get_registration_time();
	void ecall_triggered_information_record(const bool type)const;
	uint8_t ecall_get_call_state(const int32_t callId, const uint8_t state);
	uint32_t convert_u32(eCallSubState call_sub_state);
	uint32_t convert_u32(eCallMsdState msd_state);
	void  ecall_make_fast_ecall();
	
	void notify(const uint32_t event, const std::string data);
	void ecall_set_audio_device(std::string device);
    std::string ecall_get_audio_device();
    // << [LE0WNC-2616] 20230117 CK: save latest call triggered/button state, which may not be currently running Ecall/Acall
    void setLatestECallTriggeredMethodId(const uint32_t newECallTriggeredMethodId);
    void setEcallCurrentState(const uint32_t newEcallState);
    void setAcallCurrentState(const uint32_t newAcallState);
    void onBtnStateChanged(uint32_t newBtnState);
    void setECallBtnStateTypeViaBtnPressOver2sec(Poco::Timer&);
    void setACallBtnStateTypeViaBtnPressOver2sec(Poco::Timer&);
    // << [LE0WNC-2616]
    //<< [LE0WNC-9450] TracyTu, adjust redial strategy 
	bool isRedialBeyondMaxAttempts(); 
	bool tryRedial();
	bool isECallActive();
	void resetRedialAttempts(eCallState currCallState, bool isNewCall = false);
	uint32_t getRedialTimer();
	void incRedialAttempts();
    bool isLastDialAttempts()const;
	void setFirstDialTime();
	//>> [LE0WNC-9450] 
	//<< [REQ-0481764] TracyTu, the timer CALL_AUTO_ANSWER_TIME is not rearmed if the PSAP calls backs
	void setEnterWCBTime();
	void setExitWCBTime();
	void calElapsedTimeInWCB();
	uint32_t getElapsedTimeInWCB()const;
	void calRemainingTimeInWCB(uint32_t elapsedTime);
	void setRemainingTimeInWCB(uint32_t remainingTime);
	uint32_t getRemainingTimeInWCB();
	//>> [REQ-0481764]
    uint32_t getCancelationTime()const {return config.getECallConfigCancelationTime();}
    uint32_t getRegistrationTime()const {return config.getECallConfigRegistrationTime();}
    uint32_t getDialDurationTime()const {return config.getECallConfigDialDurationTime();}
    uint32_t getMsdTransmissionTime()const {return config.getECallConfigMsdTransmissionTime();}
    uint32_t getCCFT()const {return config.getECallConfigCCFT();}
    uint32_t getAutoAnswerTime()const {return config.getECallConfigAutoAnswerTime();}
    std::string getEcallNumber()const {return config.getECallConfigEcallNumber();}
    uint32_t getDialAttempsEU()const {return config.getECallConfigDialAttempsEU();}
    bool isMaunalEcallActive()const {return config.getECallConfigMaunalEcallActive();}
    bool isAutoEcallActive()const {return config.getECallConfigAutoEcallActive();}
    uint32_t getT5()const {return config.getECallConfigT5();}
    uint32_t getT6()const {return config.getECallConfigT6();}
    std::string getEcallTestNumber()const {return config.getECallConfigEcallTestNumber();}
    uint32_t getWaitingNetworkBub()const {return config.getECallConfigWaitingNetworkBub();}
    uint32_t getWaitingNetworkVB()const {return config.getECallConfigWaitingNetworkVB();}
    bool isEcallManualCanCancel()const {return config.getECallConfigEcallManualCanCancel();}
    uint32_t getUnavailableRequestTime()const {return config.getECallConfigUnavailableRequestTime();}
    uint32_t getEventLogFileExpiredTime()const {return config.getECallConfigEventLogFileExpiredTime();}
    std::string getAudioParamPath()const {return config.getECallConfigAudioParamPath();}
    std::string getAudioDevice()const {return config.getECallConfigAudioDevice();}
    bool isEcallButtonTriggerDuringOTA()const {return config.getECallConfigEcallButtonTriggerDuringOTA();}
    bool isECallAutoTriggerDuringOTA()const {return config.getECallConfigECallAutoTriggerDuringOTA();}
    uint32_t getLatestEcallTriggeredMethodId()const {return config.getECallConfigLatestEcallTriggeredMethodId();}
    int64_t getLatestEcallStartAt()const {return config.getECallConfigLatestEcallStartAt();}
    uint32_t getDataLogFileExpiredTime()const {return config.getECallConfigDataLogFileExpiredTime();} // << [LE0-13446] TracyTu, delete MSD data log file >>
    
    void setCancelationTime(uint32_t const cancelTime){config.setECallConfigCancelationTime(cancelTime);}
    void setRegistrationTime(uint32_t const registrationTime){config.setECallConfigRegistrationTime(registrationTime);}
    void setDialDurationTime(uint32_t const duration){config.setECallConfigDialDurationTime(duration);}
    void setMsdTransmissionTime(uint32_t const time) {config.setECallConfigMsdTransmissionTime(time);}
    void setCCFT(uint32_t const time){config.setECallConfigCCFT(time);}
    void setAutoAnswerTime(uint32_t const time){config.setECallConfigAutoAnswerTime(time);}
    void setEcallNumber(std::string const number){config.setECallConfigEcallNumber(number);}
    void setDialAttempsEU(uint32_t const attempts){config.setECallConfigDialAttempsEU(attempts);}
    void setMaunalEcallActive(bool const active){config.setECallConfigMaunalEcallActive(active);}
    void setAutoEcallActive(bool const active){config.setECallConfigAutoEcallActive(active);}
    void setEcallTestNumber(std::string const number){config.setECallConfigEcallTestNumber(number);}
    void setWaitingNetworkBub(uint32_t const time){config.setECallConfigWaitingNetworkBub(time);}
    void setWaitingNetworkVB(uint32_t const time){config.setECallConfigWaitingNetworkVB(time);}
    void setEcallManualCanCancel(bool const cancel){config.setECallConfigEcallManualCanCancel(cancel);}
    void setUnavailableRequestTime(uint32_t const time){config.setECallConfigUnavailableRequestTime(time);}
    void setEventLogFileExpiredTime(uint32_t const time){config.setECallConfigEventLogFileExpiredTime(time);}
    void setAudioParamPath(std::string const path){config.setECallConfigAudioParamPath(path);}
    void setAudioDevice(std::string const device){config.setECallConfigAudioDevice(device);}
    void setEcallButtonTriggerDuringOTA(bool const trigger){config.setECallConfigEcallButtonTriggerDuringOTA(trigger);}
    void setECallAutoTriggerDuringOTA(bool const trigger){config.setECallConfigECallAutoTriggerDuringOTA(trigger);}
    void setLatestEcallTriggeredMethodIdToConfig(uint32_t const id){config.setECallConfigLatestEcallTriggeredMethodId(id);}
    void setLatestEcallStartAt(int64_t const time){config.setECallConfigLatestEcallStartAt(time);}
    void setDataLogFileExpiredTime(uint32_t const time){config.setECallConfigDataLogFileExpiredTime(time);} // << [LE0-13446] TracyTu, delete MSD data log file >>

    sECallConfig getDefaultConfig() { return defaultConfig; }  // << [LE0-9841] 20231201 CK >>
    std::string getInternalParkMode()const {return internal_data.getParkMode();}
    uint32_t getInternalLifeCycleState()const {return internal_data.getLifeCycleState();}
    std::string getInternalThermalMitigation()const {return internal_data.getThermalMitigation();}
    std::string getInternalVINNumber()const {return internal_data.getVINNumber();}
    uint8_t getInternalIVSLanguage()const {return internal_data.getIVSLanguage();}
    std::string getInternalModeConfigVhl()const {return internal_data.getModeConfigVhl();} // << [LE0-10901] 20240223 ck >>
            
    void setInternalParkMode(std::string const mode){internal_data.setParkMode(mode);}
    void setInternalLifeCycleState(uint32_t const state){internal_data.setLifeCycleState(state);}
    void setInternalThermalMitigationValue(std::string const value){internal_data.setThermalMitigation(value);}
    void setInternalVINNumber(std::string const vin){internal_data.setVINNumber(vin);} // << [LE0-7781] 20230901 ck >>
    void setInternalIVSLanguage(uint8_t const language){internal_data.setIVSLanguage(language);}
    void setInternalModeConfigVhl(std::string const modeConfigVhl){internal_data.setModeConfigVhl(modeConfigVhl);} // << [LE0-10901] 20240223 ck >>
    void resetInternalParkMode(){internal_data.resetParkMode();} //<< [LE0-14407] TracyTu, eCall recovery >>
    void resetInternalLifeCycleState(){internal_data.resetLifeCycleState();} // << [LE0-10901] 20240223 ck >>
    void resetInternalThermalMitigation(){internal_data.resetThermalMitigation();} // << [LE0-10901] 20240223 ck >>
    eCallState getEcallMachineState()const { return m_state;}
    void setEcallMachineState(eCallState const state)
    {
        m_state = state;
    }
    eCallState getEcallMachineSubState()const { return m_sub_state;}
    void setEcallMachineSubState(eCallState const sub_state)
    {
        m_sub_state = sub_state;
    }
    eCallSubState getEcallCallSubState()const { return m_call_sub_state; }
    void setEcallCallSubState(eCallSubState const call_sub_state)
    {
        m_call_sub_state = call_sub_state;
    }
    int32_t getEcallCallId()const { return m_callId; }
    uint32_t getEcallButtonState()const { return m_ecall_button_state; }
    void setEcallButtonState(uint32_t const ecall_button_state)
    {
        m_ecall_button_state = ecall_button_state;
    }
    Fih::Xcall::ButtonState_Type getEcallButtonStateType()const { return ecallBtnStateType; }
    Fih::Xcall::ButtonState_Type getAcallButtonStateType()const { return acallBtnStateType; } 
    uint32_t getEcallState()const { return m_ecall_state; }
    void setEcallState(uint32_t const ecall_state)
    {
        m_ecall_state = ecall_state;
    }
    std::string getBackupBattery()const { return m_back_up_battery; }
    void setBackupBattery(std::string const back_up_battery) 
    {
        m_back_up_battery = back_up_battery;
    }
    std::string getVehicleBattery()const { return m_vehicle_battery; }
    void setVehicleBattery(std::string const vehicle_battery) 
    {
        m_vehicle_battery = vehicle_battery;
    }
    bool getDiagTestEcallSession()const { return m_diag_test_ecall_session;}
    void setDiagTestEcallSession(bool const diag_test_ecall_session)
    {
        m_diag_test_ecall_session = diag_test_ecall_session;
    }
    uint32_t getEcallCurrentState()const { return ecall_current_state; }
    uint32_t getBcallCurrentState()const { return bcall_current_state; }
    uint64_t getEnterWCBEpochTime()const { return m_enter_WCB_epoch_time; }
    void setEnterWCBEpochTime(uint64_t const enter_WCB_epoch_time)
    {
        m_enter_WCB_epoch_time = enter_WCB_epoch_time;
    }
    
    void enterECallStateECallCallOff();
    void enterECallStateECallManualTrigger();
    void enterECallStateECallRegistration();
    void enterECallStateECallCall();
    void enterECallStateECallMSDTransfer();
    void enterECallStateECallVoiceCommunication();
    void enterECallStateECallWCB();
    void enterECallStateECallWCBIdle();
    void enterECallStateECallWCBManualTrigger();
    //<< [LE0-6399] TracyTu, Play beep after pressing the button for two seconds 
    bool isPrompt1Played()const { return m_is_prompt1_played; }
    void processLongPressInEcallWCB(); 
    //>> [LE0-6399]
    void processIncomingCallInEcallWCB(CeCallFunction::sCallState callState);
    void updateHpMicroTestState(const bool duringTest); // << [LE0-13731] 20240514 CK >>
    CallStatus3A9 export3A9StateV2(); // << [LE0-13731] 20240514 CK >>
    void update3A9StateV2(); // << [LE0-13731] 20240514 CK >>
    //<< [V3-REQ] TracyTu, for xCall status
    void reportFault();
    void clearFault();
    void setXcallStatus(XcallStatus status); 
    //<< [LE0-10138] TracyTu, eCall trace develop
    void setPowerSource(uint8_t power_source)
    {
        m_power_source = power_source;
    }
    uint8_t getPowerSource()
    {
        return m_power_source;
    }
    //>> [LE0-10138]
    bool isXcallStatusFalut() const { return m_Fault_triggered; } // << [LE0-9412] 20240109 CK >>
    bool isNeedResyncXCallStatus() const { return mIsNeedResyncXCallStatus; } // << [LE0-9412] 20240109 CK >>
    void setNeedResyncXCallStatus() { mIsNeedResyncXCallStatus = true; } // << [LE0-9412] 20240109 CK >>
    // << [LE0-12080] TracyTu, Support one image 
    bool isEcallTypePE112() const;
    uint16_t getEcallType() const { return m_ecall_type; }
    void setEcallType(uint16_t const type){ m_ecall_type = type; }
    uint8_t getEcallPushPresence() const { return m_ecall_push_presence; }
    void setEcallPushPresence(uint8_t const push_presence){ m_ecall_push_presence = push_presence; }
    // >> [LE0-12080] 
	// << [LE0-13446] TracyTu, delete MSD data log file
    void setEcallDataLogFileUpdateTime(uint64_t const time);
    uint64_t getEcallDataLogFileUpdateTime() const;
    uint32_t getEcallDataFileElaspedTime();
    void checkEcallDataFileExpiry();
    uint32_t getDataLogFileExpiredSecond() const;
    // >> [LE0-13446] 
    //<< [LE0-14407] TracyTu, eCall recovery 
    void setEventRcvStatus(genericEvent event, bool rcvStatus);
    bool getEventRcvStatus(genericEvent event);
    bool ready() {
        _logger.information("%s", fmt::format("ready allInitConditionsAvail:{}", allInitConditionsAvail())); 
        return allInitConditionsAvail(); 
    }
    void initStartUp();
    bool isNeedToRetriggerECallAfterReboot() const { return m_triggerECallAfterReboot; }
    void setNeedToRetriggerECallAfterReboot(bool const triggerECall) {m_triggerECallAfterReboot = triggerECall;} // << [LE0-11009][LE0-11039][LE022-983] 20240229 CK >>
    void handleLcmParkMode(std::string park_mode);
    void handleLcmState(uint32_t lcm_state);
    void handleLcmThermal(std::string thermal);
    //>> [LE0-14407]
    //<< [LE0-10138] TracyTu, eCall trace develop (phase 5)
    void setMsdSendType(uint8_t const type) { m_msdSendType = type; }
    uint8_t getMsdSendType() const { return m_msdSendType; }
    void setMsdTimeoutType(uint8_t const type) { m_msdTimeoutType = type; }
    uint8_t getMsdTimeoutType() const { return m_msdTimeoutType; }
    //>> [LE0-10138] 
    bool isWCBIdle(); // << [LE0-15588] 20240823 MaoJunYan >>

private:
    bool canStore()const {return m_Fault_triggered == false;}
    void saveXcallStatusToMCU(XcallStatus status);
    //>> [V3-REQ]
    // << [LE022-2691] 20240524 ZelosZSCao
    bool isPhoneNumberNull() const;
    bool isAnyCellFound() const;
    void pollingCellInfo(bool start) const;
    // >> [LE022-2691]
	//<< [LE0-14407] TracyTu, eCall recovery 
    bool allInitEventsRcved() { return getEventRcvStatus(PARK_MODE) && getEventRcvStatus(LCM_STATE) && getEventRcvStatus(THERMAL); }
    bool allInitConditionsAvail() { return allInitEventsRcved();}
    bool needEcallRecovery() {
        return isNeedToRetriggerECallAfterReboot() || isEcallButtonTriggerDuringOTA() || isECallAutoTriggerDuringOTA();
    } 
    void notifyToCheckRecovery();
    void recoverLcmParkMode(std::string park_mode);
    void recoverLcmState(uint32_t lcm_state);
    void recoverLcmThermal(std::string thermal);
	//>> [LE0-14407]
    void ecall_stop_msd_transfer(const uint32_t reason); // << [LE0-15382] TracyTu, Add msd state when psap request >>

private:
    Poco::Timer m_statetimer;
	Poco::Timer m_redialtimer;
	Poco::Timer m_prompttimer;
    Poco::Timer m_msdExpreTimer; // << [LE0WNC-3009] 20230207 ZelosZSCao >>
    Poco::Timer m_ecallBtnPressOver2secTimer;
    Poco::Timer m_acallBtnPressOver2secTimer;
    Poco::Timer m_beepTimer;//<<[LE0-6399][LE0-8913]yusanhsu>>
    Poco::Timer m_getSignalStrengthTimer; // << [LE0-10138] 20231226 CK >>
    Poco::Timer m_registCellTimer; // << [LE022-2691] 20240524 ZelosZSCao >>
    Poco::Timer m_msdRequestTimer; // << [LE0-15382] TracyTu, Add msd state when psap request >>
    sECallConfig config;
    sECallConfig defaultConfig;  // << [LE0-9841] 20231201 CK >>
    sECallInternalData internal_data;
    eCallState m_state{eCallState::ECALL_OFF};
    eCallState m_sub_state{eCallState::ECALL_WCB_ECALL_IDLE};
	eCallMsdState m_msd_state{eCallMsdState::ECALL_MSD_NONE};
	eCallSubState m_call_sub_state{eCallSubState::ECALL_SUB_NONE};
    eCallManager* const pecallMgr;
	CLogger& _logger;
    std::int64_t call_back_time{0};
	int32_t m_callId{0};
	uint32_t dial_attempts{0U};
    uint32_t m_ecall_button_state{XCALL_ECALL_BTN_RELEASE};
    // << [LE0WNC-1152] 20230206 CK
    // >> [LE0WNC-1152]
    // << [LE0WNC-2616] 20230117 CK: save latest call triggered/button state, which may not be currently running Ecall/Acall
    uint32_t latestEcallTriggeredMethodId{0U};
    int64_t latestEcallStartedAt{0};
    Fih::Xcall::ButtonState_Type ecallBtnStateType {Fih::Xcall::ButtonState_Type::NOT_PRESSED};
    Fih::Xcall::ButtonState_Type acallBtnStateType {Fih::Xcall::ButtonState_Type::NOT_PRESSED};
    // >> [LE0WNC-2616]
    uint32_t m_ecall_state{XCALL_STATE_ECALL_OFF};
	uint32_t ecall_current_state{XCALL_STATE_ECALL_OFF};
	uint32_t bcall_current_state{XCALL_STATE_BCALL_OFF};
	std::string m_back_up_battery{"normal"};
	std::string m_vehicle_battery{"normal"};
	bool m_diag_test_ecall_session{false}; 

	//<< [LE0WNC-9450] TracyTu, adjust redial strategy 
	const uint32_t redial_delay_time = 5U; //5s
	Poco::LocalDateTime m_first_dial_time; 
	eCallState eCall_state_for_redial{eCallState::ECALL_OFF};
	//>> [LE0WNC-9450]
  	//<< [REQ-0481764] TracyTu, the timer CALL_AUTO_ANSWER_TIME is not rearmed if the PSAP calls backs  
	Poco::LocalDateTime m_enter_WCB_time; 
	uint64_t m_enter_WCB_epoch_time;  //<< [REQ-0481764] TracyTu, recover call back time when macchina crash
	Poco::LocalDateTime m_exit_WCB_time; 
	uint32_t m_elapsed_time_in_WCB;
	uint32_t m_remaining_time_in_WCB;
	bool m_is_already_in_WCB{false};
	//>> [REQ-0481764]

    bool m_is_prompt1_played{false}; //<< [LE0-6399] TracyTu, Play beep after pressing the button for two seconds >>
    CallStatus3A9 m_latestCallStatus3A9{CallStatus3A9::UNKNOWN}; // << [LE0-13731] 20240514 CK >>
    //<< [V3-REQ] TracyTu, for xCall status
    XcallStatus m_status{XcallStatus::NO_XCALL};
    bool m_Fault_triggered{false};
    bool mIsNeedResyncXCallStatus{false}; // << [LE0-9412] 20240109 CK >>
    //>> [V3-REQ]
    uint8_t m_power_source{BBS_VEHICLE};  //<< [LE0-10138] TracyTu, eCall trace develop >>
    // << [LE0-12080] TracyTu, Support one image 
    const uint16_t RTBM_ECALL_TYPE_PE112{0xFFFEU}; 
    uint16_t m_ecall_type{0xFFFFU};     
    uint8_t m_ecall_push_presence{0U};
    // >> [LE0-12080]
    // << [LE0-13446] TracyTu, delete MSD data log file
    const uint64_t INVALID_DATA_LOGFILE_TIME_SEC{0xFFFFFFFFU}; 
    uint64_t m_data_logfile_update_time;   
    // >> [LE0-13446]  
	//<< [LE0-14407] TracyTu, eCall recovery 
    bool m_triggerECallAfterReboot{false}; 
    std::bitset<32U> m_event_rcv_status{0};
    //>> [LE0-14407]
    
    //<< [LE0-10138] TracyTu, eCall trace develop (phase 5) 
    uint8_t m_msdSendType{BBS_MSD_SEND_DEFAULT};
    uint8_t m_msdTimeoutType{BBS_MSD_SEND_TIMEOUT_DEFAULT};
    //>> [LE0-10138]
};

} } // namespace MD::eCallMgr

#endif // CECALLMACHINE_H
