#include "CeCallMachine.h"
#include "eCallManager.h"
#include "fmt/core.h"
#include "Poco/Timer.h"
#include "CeCallCmd.h"
#include "CeCallHandler.h"
#include "CeCallFunction.h"
#include "CeCallRemoteServer.h"
//LouisLIBMerge modify library modification 2022/08/16
#include "radiofih/fih_sdk_ecall_api.h"
#include "CeCallXmlConfig.h"
#include "CeCallEventLog.h"
#include "CeCallMSD.h"
#include "CeCallAudio.h"
#include "CeCallConfig.h"
#include "CeFihSysApiInterface.h"
#include "CeCallCan.h"
//<< [LE0WNC-9450] TracyTu, adjust redial strategy
#include <cmath>
#include <algorithm>
//>> [LE0WNC-9450]

#include <chrono>

using Poco::Timer;
using Poco::TimerCallback;
using Poco::LocalDateTime;
using Poco::DateTimeFormatter;
using Fih::Xcall::ButtonState_Type;

namespace MD {
namespace eCallMgr {

CeCallMachine::CeCallMachine(eCallManager* const pMgr) 
	:pecallMgr(pMgr)
	,_logger(CLogger::newInstance("MCHN"))
{
}
CeCallMachine::~CeCallMachine()
{	 
}
void CeCallMachine::stopTimer() {
    try {
        m_statetimer.stop();
        m_redialtimer.stop();
        m_prompttimer.stop();
        m_msdExpreTimer.stop();
        m_ecallBtnPressOver2secTimer.stop();
        m_acallBtnPressOver2secTimer.stop();
		m_beepTimer.stop(); //<<[LE0-6399][LE0-8913]yusanhsu>>
        m_registCellTimer.stop(); // << [LE022-2691] 20240524 ZelosZSCao >>
		m_msdRequestTimer.stop(); // << [LE0-15382] TracyTu, Add msd state when psap request >>
        m_getSignalStrengthTimer.stop(); // << [LE022-5418] 20241023 EasonCCLiao >>
	}
	catch (Poco::Exception &exc)
	{
		_logger.error("%s", fmt::format("stopTimer fail, e::{}", exc.displayText()));
	}
	catch (...)
	{
		_logger.error("%s", fmt::format("stopTimer Exception"));
	}
}
void CeCallMachine::ecall_trans_state(eCallState nextstate)
{
	_logger.imp("%s",fmt::format("[AT] trans_state to {}", nextstate)); // << [LE022-5019] 20240925 EasonCCLiao >>
	switch(nextstate)
	{
        case eCallState::ECALL_OFF:
		{
            enterECallStateECallCallOff();
			break;
		}
		case eCallState::ECALL_MANUAL_TRIGGERED:
		{
			enterECallStateECallManualTrigger();
			break;
		}
		case eCallState::ECALL_REGISTRATION:
		{
			enterECallStateECallRegistration();
			break;
		}
		case eCallState::ECALL_CALL:
		{
            enterECallStateECallCall();
            break;
		}
        case eCallState::ECALL_MSD_TRANSFER:
		{	
			enterECallStateECallMSDTransfer();
			break;
		}
		case eCallState::ECALL_VOICE_COMMUNICATION:
		{
			enterECallStateECallVoiceCommunication();
			break;
		}
		case eCallState::ECALL_WAITING_FOR_CALLBACK:
		{
            enterECallStateECallWCB();
			break;
		}
		case eCallState::ECALL_WCB_ECALL_IDLE:
		{
			enterECallStateECallWCBIdle();
			break;
		}
		case eCallState::ECALL_WCB_ECALL_MANUAL_TRIGGERED:
		{
			enterECallStateECallWCBManualTrigger();
			break;
		}
	}
}
void CeCallMachine::ecall_triggered_information_record(const bool type)const
{
    pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_TRIGGERED());
    pecallMgr->peCallHandler()->putStringToStateQ(ECALL_LED_GREEN_FIXED_LIGHT, std::string(""));
    pecallMgr->peCallRemoteServer()->notifyRemoteCommand(XCALL_STATE_ECALL_TRIGGERED, std::string(""));
	pecallMgr->peCallMSD()->set_data_automatic_activation(type);
    pecallMgr->peCallXmlConfig()->saveEcallAutomaticActivation(type);
}
uint32_t CeCallMachine::ecall_get_registration_time()
{
    uint32_t time = 0U;
	if(getVehicleBattery().compare(std::string("normal")) == 0)
	{
		time =  getRegistrationTime() + getWaitingNetworkVB() * 60U;
	}		
	else if(getBackupBattery().compare(std::string("normal")) == 0)
	{
		time =  getRegistrationTime() + getWaitingNetworkBub() * 60U;
	}
	else
	{
		time =  getRegistrationTime();
	}
	_logger.information("%s",fmt::format("ecall_get_registration_time: {} ", time));
	return time;
}

void CeCallMachine::ecall_process_off(const CeCallState::Ptr peCallState)
{
	uint8_t trigger_type = pecallMgr->peCallTrace()->convertBBSTriggerType(peCallState->getString()); //<< [LE0-10138] TracyTu, eCall trace develop >>
	_logger.notice("%s",fmt::format("ecall_process_off ID:{} trigger_type: {} ", peCallState->getId(), trigger_type));
	switch (peCallState->getId())
	{
		case ECALL_FLOW_MANUAL_TRIGGER:
		{
			_logger.imp("ECALL_FLOW_MANUAL_TRIGGER");
			ecall_triggered_information_record(false);
			pecallMgr->peCallTrace()->setBBSTriggerType(trigger_type); //<< [LE0-10138] TracyTu, eCall trace develop >>
			setXcallStatus(XcallStatus::MANUAL_ECALL_TRIGGERED);  //<< [V3-REQ] TracyTu, for xCall status >>
            ecall_trans_state(eCallState::ECALL_REGISTRATION);
			break;
		}
        case ECALL_FLOW_BTN_LONG_PRESS:
		{
			_logger.imp("ECALL_FLOW_BTN_LONG_PRESS");
    		ecall_triggered_information_record(false);			
			pecallMgr->peCallTrace()->setBBSTriggerType(trigger_type); //<< [LE0-10138] TracyTu, eCall trace develop >>
			setXcallStatus(XcallStatus::MANUAL_ECALL_TRIGGERED);  //<< [V3-REQ] TracyTu, for xCall status >>
            if(false == isEcallManualCanCancel())
            {
                ecall_trans_state(eCallState::ECALL_REGISTRATION);
            }
			else
			{
			    ecall_trans_state(eCallState::ECALL_MANUAL_TRIGGERED);
			}
			break;
		}
        case ECALL_FLOW_AUTO_TRIGGER:
		{
			_logger.imp("ECALL_FLOW_AUTO_TRIGGER");
            ecall_triggered_information_record(true);
			pecallMgr->peCallTrace()->setBBSTriggerType(trigger_type); //<< [LE0-10138] TracyTu, eCall trace develop >>
			setXcallStatus(XcallStatus::AUTO_ECALL_TRIGGERED);  //<< [V3-REQ] TracyTu, for xCall status >>
			ecall_trans_state(eCallState::ECALL_REGISTRATION);
			break;
		}
		case ECALL_FLOW_AIR_BAG_MESSAGE:
		{
			_logger.imp("ECALL_FLOW_AIR_BAG_MESSAGE");
            ecall_triggered_information_record(true);
			pecallMgr->peCallTrace()->setBBSTriggerType(trigger_type); //<< [LE0-10138] TracyTu, eCall trace develop >>
			setXcallStatus(XcallStatus::AUTO_ECALL_TRIGGERED);  //<< [V3-REQ] TracyTu, for xCall status >>
			ecall_trans_state(eCallState::ECALL_REGISTRATION);
			break;
		}
		//<< [REQ-0481764] TracyTu, recover call back time when macchina crash
		case ECALL_FLOW_CALL_CRASH_RECOVERY_IND:
		{
			_logger.notice( "%s",fmt::format("ECALL_FLOW_CALL_CRASH_RECOVERY_IND, ecall_state:{}", getEcallState()));
			if(XCALL_STATE_ECALL_WAITING_FOR_CALLBACK == getEcallState())
			{
			   m_is_already_in_WCB  = true;
			   setExitWCBTime();
			   _logger.imp( "%s",fmt::format("ECALL_FLOW_CALL_CRASH_RECOVERY_IND, m_remaining_time_in_WCB_test:{}", m_remaining_time_in_WCB));
				//<< [V3-REQ] TracyTu, for xCall status
				if(m_remaining_time_in_WCB <= 0U)
				{
					ecall_trans_state(eCallState::ECALL_OFF);
				}
				else 
				{
					XcallStatus status = (false == pecallMgr->peCallMSD()->get_msd_data().automatic_activation) ? XcallStatus::MANUAL_ECALL_TRIGGERED : XcallStatus::AUTO_ECALL_TRIGGERED;
					setXcallStatus(status);
					ecall_trans_state(eCallState::ECALL_WAITING_FOR_CALLBACK);

				}
			   //>> [V3-REQ] 
            }
			break;
		}
		//>> [REQ-0481764]
		default:
			break;
	}
}
void CeCallMachine::ecall_manual_cancel_timeout(Poco::Timer&)
{
	pecallMgr->peCallTrace()->setBBSSingleTag(BBS_ECALL_CANCELLATION_TIME, 0);	//<< [LE0-10138] TracyTu, eCall trace develop >>		
	if( eCallState::ECALL_MANUAL_TRIGGERED == getEcallMachineState())
	{
		_logger.notice("ecall_manual_cancel_timeout in ECALL_MANUAL_TRIGGERED ");
		pecallMgr->peCallHandler()->putStringToStateQ(ECALL_FLOW_MANUAL_CANCEL_TIMEOUT, std::string(""));
	}
    else if((eCallState::ECALL_WAITING_FOR_CALLBACK == getEcallMachineState()) && (eCallState::ECALL_WCB_ECALL_MANUAL_TRIGGERED == getEcallMachineSubState()))
	{
		_logger.notice("ecall_manual_cancel_timeout in ECALL_WAITING_FOR_CALLBACK ");
        pecallMgr->peCallAudio()->playAudioWithLanguage008(); //[LE0-6210][LE0-6062][LE0-5724][LE0-5671][LE0WNC-2050] yusanhsu, according to REQ-0481825, tone 8 should be played once the call is confirmed
		pecallMgr->peCallHandler()->putStringToStateQ(ECALL_FLOW_MANUAL_CANCEL_TIMEOUT, std::string(""));
		
	}
	else
	{
		_logger.warning("%s",fmt::format("ecall_manual_cancel_timeout on wrong state: {} ", getEcallMachineState()));
	}
}
void CeCallMachine::ecall_registration_timeout(Poco::Timer&)
{
	if(eCallState::ECALL_REGISTRATION == getEcallMachineState())
	{
		_logger.notice("ecall_registration_timeout");
		//<< [LE0-10138] TracyTu, eCall trace develop
		std::vector<eCallTraceRecord> records{};
		records.emplace_back(eCallTraceRecord{BBS_ECALL_REGISTRATION_TIME, 0});	
		records.emplace_back(eCallTraceRecord{BBS_ECALL_POWER_SOURCE, getPowerSource()});	
		pecallMgr->peCallTrace()->setEcallTraceLog(static_cast<uint8_t>(pecallMgr->peCallMachine()->getEcallMachineState()), records);
		//>> [LE0-10138]
		pecallMgr->peCallHandler()->putStringToStateQ(ECALL_FLOW_REGISTRATION_TIMEOUT, std::string(""));
	}
	else
	{
		_logger.warning("%s",fmt::format("ecall_registration_timeout on wrong state: {} ", getEcallMachineState()));
	}
}

// << [LE022-2691] 20240524 ZelosZSCao
void CeCallMachine::ecall_cellinfo_polling_timeout(Poco::Timer&) {
    _logger.notice("%s", fmt::format("[net_reg_polling] ecall_cellinfo_polling_timeout"));
    if (isAnyCellFound()) {
        pollingCellInfo(false);
    }
}
// >> [LE022-2691]

void CeCallMachine::ecall_process_manual_trigger(const CeCallState::Ptr peCallState)
{
	switch (peCallState->getId())
	{
        case ECALL_FLOW_MANUAL_CANCEL:
		case ECALL_FLOW_BTN_SHORT_PRESS:
		{
			_logger.notice("ECALL_FLOW_MANUAL_CANCEL");
			(void)pecallMgr->peCallAudio()->play_audio_with_language("007");
			pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_CANCELED());
			ecall_trans_state(eCallState::ECALL_OFF);
			pecallMgr->peCallHandler()->putStringToStateQ(ECALL_FLOW_FINISHED, std::string(""));
			break;
		}
		case ECALL_FLOW_MANUAL_CANCEL_TIMEOUT:
		{
			_logger.notice("ECALL_FLOW_MANUAL_CANCEL_TIMEOUT");
			ecall_trans_state(eCallState::ECALL_REGISTRATION);
			break;
		}
		//<< [LE0-9791] TracyTu, handle demoapp crash ind in call 
		case ECALL_FLOW_URC_DEMOAPP_STATUS_IND:
		{
			pecallMgr->peCallTrace()->setBBSSingleTag(BBS_ECALL_DEMO_APP_CRASH, 1U); //<< [LE0-10138] TracyTu, eCall trace develop (phase 4) >>
			_logger.notice("%s", fmt::format("ECALL_FLOW_URC_DEMOAPP_STATUS_IND: call state: {}", getEcallMachineState()));
			break;
		}
		//>> [LE0-9791]
		default:
			break;
	}
}

bool CeCallMachine::ecall_get_cellular_network_srv_state(std::any obj) const {
// << [LE022-2691] 20240524 ZelosZSCao
    bool ret = false;

    const fihGetVoiceRegistrationRsp_t pVoiceRegistration = std::any_cast<fihGetVoiceRegistrationRsp_t>(obj);
    pecallMgr->peCallEventLog()->notifyUpdateEvent(CELLULAR_NETWORK_STATE, static_cast<uint32_t>(pVoiceRegistration.i16ErrorCode), pVoiceRegistration.eSrvState);
    pecallMgr->peCallTrace()->setBBSVoiceRegResponce(pVoiceRegistration); // << [LE0-10138] 20231225 CK >>
    if (FIH_NO_ERR == pVoiceRegistration.i16ErrorCode) {
        _logger.notice("%s", fmt::format("[net_reg_polling] ecall_get_cellular_network_srv_state:{}", pVoiceRegistration.eSrvState));
        switch (pVoiceRegistration.eSrvState) {
            case FIH_ECALL_CELLULAR_NETWORK_SRV_STATE_E::FIH_ECALL_CELLULAR_NETWORK_SRV_NO:
            case FIH_ECALL_CELLULAR_NETWORK_SRV_STATE_E::FIH_ECALL_CELLULAR_NETWORK_SRV_SEARCHING:
                if (isPhoneNumberNull()) {
                    if (isAnyCellFound()) {
                        ret = true;
                    } else {
                        pollingCellInfo(true);
                    }
                }
                break;
            case FIH_ECALL_CELLULAR_NETWORK_SRV_STATE_E::FIH_ECALL_CELLULAR_NETWORK_SRV_ROAMING:
            case FIH_ECALL_CELLULAR_NETWORK_SRV_STATE_E::FIH_ECALL_CELLULAR_NETWORK_SRV_REGISTERED:
                ret = true;
                break;
            case FIH_ECALL_CELLULAR_NETWORK_SRV_STATE_E::FIH_ECALL_CELLULAR_NETWORK_SRV_DENIED:
            case FIH_ECALL_CELLULAR_NETWORK_SRV_STATE_E::FIH_ECALL_CELLULAR_NETWORK_SRV_UNKNOWN:
            default: // FIH_ECALL_CELLULAR_NETWORK_SRV_DENIED
                ret = false;
                break;
        }
    }
    return ret;
// >> [LE022-2691]
}

void CeCallMachine::ecall_process_registration(const CeCallState::Ptr peCallState)
{
	_logger.information("%s", fmt::format("[ecall_process_registration] call_sub_state:{}", getEcallCallSubState()));
	switch (peCallState->getId())
	{
        // << [LE022-2691] 20240524 ZelosZSCao
        case ECALL_FLOW_REGISTRATION_POLLING_START: {
            _logger.notice("[net_reg_polling] ecall_process_registration ECALL_FLOW_REGISTRATION_POLLING_START");
            m_registCellTimer.stop();
            m_registCellTimer.setStartInterval(20 * 1000);
            m_registCellTimer.setPeriodicInterval(20 * 1000);
            m_registCellTimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_cellinfo_polling_timeout), "ecall_cellinfo_polling_timer");
            break;
        }
        case ECALL_FLOW_REGISTRATION_POLLING_SUCCESS:
            // peCallState->getAny() not has value
            _logger.notice("%s", fmt::format("[net_reg_polling] ecall_process_registration ECALL_FLOW_REGISTRATION_POLLING_SUCCESS, has_value:{}", peCallState->getAny().has_value()));
            [[fallthrough]];
        case ECALL_FLOW_REGISTRATION_SUCCESS: {
            // Get the ecall registration state response done
            _logger.notice("ECALL_FLOW_REGISTRATION_SUCCESS");
            const bool state = peCallState->getAny().has_value() ? ecall_get_cellular_network_srv_state(peCallState->getAny()) : true;
            if (state) {
                m_registCellTimer.stop();
                m_prompttimer.stop();
                setEcallCallSubState(eCallSubState::ECALL_SUB_NONE);
                ecall_trans_state(eCallState::ECALL_CALL);
            }
            break;
        }
        // >> [LE022-2691]
		case ECALL_FLOW_REGISTRATION_TIMEOUT:
		{
			_logger.notice("ECALL_FLOW_REGISTRATION_TIMEOUT");
            m_registCellTimer.stop(); // << [LE022-2691] 20240524 ZelosZSCao >>
			m_prompttimer.stop();
			pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_FAILED());
			(void)pecallMgr->peCallAudio()->play_audio_with_language("010");
			ecall_trans_state(eCallState::ECALL_OFF);
			pecallMgr->peCallHandler()->putStringToStateQ(ECALL_FLOW_FINISHED, std::string(""));
			break;
		}
		//<< [LE0-9791] TracyTu, handle demoapp crash ind in call
		case ECALL_FLOW_URC_DEMOAPP_STATUS_IND:
		{
			_logger.notice("%s", fmt::format("ECALL_FLOW_URC_DEMOAPP_STATUS_IND: call state: {}", getEcallMachineState()));
			pecallMgr->peCallTrace()->setBBSSingleTag(BBS_ECALL_DEMO_APP_CRASH, 1U); //<< [LE0-10138] TracyTu, eCall trace develop (phase 4) >>
			pecallMgr->peCallFunction()->ecall_cellularnetwork_GetVoiceRegistration();
			break;
		}
		//>> [LE0-9791] 
		default:
			break;
	}

}

// << [LE0-15382] TracyTu, Add msd state when psap request
void CeCallMachine::ecall_stop_msd_transfer(const uint32_t reason)
{
	_logger.notice("%s", fmt::format("ecall_stop_msd_transfer: call state: {}, reason: {}", getEcallMachineState(), reason));
	m_msdRequestTimer.stop();
	pecallMgr->peCallFunction()->ecall_reset_ivs();
	m_msd_state = eCallMsdState::ECALL_MSD_NONE;
}
// >> [LE0-15382] 

void CeCallMachine::ecall_comm_urc_status(const uint32_t state, const int32_t callid)
{
    switch (state)
	{
		case static_cast<uint32_t>(FIH_ECALL_DISCONNECTED):
		{
            _logger.notice("FIH_ECALL_DISCONNECTED");
			pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_FINISHED());
			pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(eCallSubState::ECALL_SUB_DISCONNECTED)); 
			setEcallCallSubState(eCallSubState::ECALL_SUB_NONE);
			(void)pecallMgr->peCallAudio()->play_audio_with_language("011");
			ecall_trans_state(eCallState::ECALL_WAITING_FOR_CALLBACK);		
			break;
		}
		case static_cast<uint32_t>(FIH_ECALL_SENDING_START):
		{
            _logger.notice("FIH_ECALL_SENDING_START");
			m_msd_state = eCallMsdState::ECALL_MSD_SENDING_START;
			pecallMgr->peCallEventLog()->notifyUpdateEvent(MSD_TRANSMISSION_STATE, convert_u32(m_msd_state)); 
			pecallMgr->peCallFunction()->ecall_set_msd(callid);
			//<< [LE0-10138] TracyTu, eCall trace develop (phase 5) 
			setMsdSendType(BBS_MSD_SEND_REQUEST);  
			pecallMgr->peCallTrace()->setBBSMsdTag(static_cast<uint8_t>(m_msd_state));
			//>> [LE0-10138] 
			setMsdTimeoutType(BBS_MSD_SEND_TIMEOUT_TRANSMISSION); //<< [LE0-10138] TracyTu, eCall trace develop (phase 5) >>
			m_msdRequestTimer.stop();
			m_msdRequestTimer.setStartInterval(static_cast<long>(getMsdTransmissionTime()) * 1000); /* <<[LE0-7984] yusanhsu, add 500ms for timing issue*/
			m_msdRequestTimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_msd_transfer_timeout), "ecall_msd_transfer_timer");
			break;
		}
		// << [LE0-15382] TracyTu, Add msd state when psap request
		case static_cast<uint32_t>(FIH_ECALL_SENDING_MSD):
		{
            _logger.notice("FIH_ECALL_SENDING_MSD");
			m_msd_state = eCallMsdState::ECALL_MSD_SENDING_MSD;
			pecallMgr->peCallEventLog()->notifyUpdateEvent(MSD_TRANSMISSION_STATE, convert_u32(m_msd_state)); 
			pecallMgr->peCallTrace()->setBBSMsdTag(static_cast<uint8_t>(m_msd_state)); //<< [LE0-10138] TracyTu, eCall trace develop (phase 5)  >>
			setMsdTimeoutType(BBS_MSD_SEND_TIMEOUT_TRANSMISSION); //<< [LE0-10138] TracyTu, eCall trace develop (phase 5) >>
			m_msdRequestTimer.stop();
			m_msdRequestTimer.setStartInterval(static_cast<long>(getMsdTransmissionTime()) * 1000); /* <<[LE0-7984] yusanhsu, add 500ms for timing issue*/
			m_msdRequestTimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_msd_transfer_timeout), "ecall_msd_transfer_timer");

			break;
		}
		case static_cast<uint32_t>(FIH_ECALL_LLACK_RECEIVED):
		{
            _logger.notice("FIH_ECALL_LLACK_RECEIVED");
			m_msd_state = eCallMsdState::ECALL_MSD_SENDING_LLACK_RECEIVED;
			pecallMgr->peCallEventLog()->notifyUpdateEvent(MSD_TRANSMISSION_STATE, convert_u32(m_msd_state)); 
			pecallMgr->peCallTrace()->setBBSMsdTag(static_cast<uint8_t>(m_msd_state)); //<< [LE0-10138] TracyTu, eCall trace develop (phase 5)  >>
			setMsdTimeoutType(BBS_MSD_SEND_TIMEOUT_T6); //<< [LE0-10138] TracyTu, eCall trace develop (phase 5) >>
			m_msdRequestTimer.stop();
			m_msdRequestTimer.setStartInterval(static_cast<long>(getT6()) * 1000); /* <<[LE0-7984] yusanhsu, add 500ms for timing issue*/
			m_msdRequestTimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_msd_transfer_timeout), "ecall_msd_transfer_timer");
			break;
		}
		case  static_cast<uint32_t>(FIH_ECALL_ALACK_POSITIVE_RECEIVED):
		{
            _logger.notice(" FIH_ECALL_ALACK_POSITIVE_RECEIVED");
			m_msd_state = eCallMsdState::ECALL_MSD_SENDING_ALACK_POSITION_RECEIVED;
			pecallMgr->peCallEventLog()->notifyUpdateEvent(MSD_TRANSMISSION_STATE, convert_u32(m_msd_state)); 
            // << [LE0WNC-3009] 20230207 ZelosZSCao: erase MSD log file after sending ack, REQ-0315113 A
            pecallMgr->peCallMSD()->remvoe_msd_log();
            _logger.information("ECALL MSD log stop timer");
            m_msdExpreTimer.stop();
			setEcallDataLogFileUpdateTime(INVALID_DATA_LOGFILE_TIME_SEC); // << [LE0-13446] TracyTu, delete MSD data log file >>
            // >> [LE0WNC-3009]
            _logger.imp("%s", fmt::format("[AT] start trans_state to ECALL_VOICE_COMMUNICATION: out call, ECALL_ALACK_POSITIVE_RECEIVED")); // << [LE022-5019][LE0WNC-9171] 20240925 EasonCCLiao >>
			pecallMgr->peCallTrace()->setBBSMsdTag(static_cast<uint8_t>(m_msd_state)); //<< [LE0-10138] TracyTu, eCall trace develop (phase 5)  >>
			ecall_stop_msd_transfer(state);
			break;
		}
		// >> [LE0-15382]
		case static_cast<uint32_t>(FIH_ECALL_ALACK_CLEARDOWN_RECEIVED):
		{
            _logger.notice("FIH_ECALL_ALACK_CLEARDOWN_RECEIVED");
			ecall_stop_msd_transfer(state);
			pecallMgr->peCallFunction()->ecall_HangUp();

			pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_FINISHED());
			(void)pecallMgr->peCallAudio()->play_audio_with_language("011");
				
			m_msd_state = eCallMsdState::ECALL_MSD_SENDING_ALACK_CLEARDOWN_RECEIVED;
			pecallMgr->peCallEventLog()->notifyUpdateEvent(MSD_TRANSMISSION_STATE, convert_u32(m_msd_state)); 

            pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(eCallSubState::ECALL_SUB_DISCONNECTED)); 
			setEcallCallSubState(eCallSubState::ECALL_SUB_NONE);
			pecallMgr->peCallTrace()->setBBSMsdTag(static_cast<uint8_t>(m_msd_state)); //<< [LE0-10138] TracyTu, eCall trace develop (phase 5)  >>
			ecall_trans_state(eCallState::ECALL_WAITING_FOR_CALLBACK);	
			break;
		}
		//<< [LE0WNC-3099] TracyTu, prompt "9"didn't played
		//<< [LE0WNC-9450] TracyTu, adjust redial strategy >>
		case static_cast<uint32_t>(FIH_ECALL_ABNORMAL_HANGUP):
		{
			_logger.notice("FIH_ECALL_ABNORMAL_HANGUP:in communication state");
			setEcallCallSubState(eCallSubState::ECALL_SUB_ABNORMAL_HANGUP);
			pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(getEcallCallSubState()));
		    		
			if(tryRedial())
			{
				pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_CALL_DROPPED());
				pecallMgr->peCallAudio()->playAudioWithLanguage009(); // << [LE0-7069] 20230904 CK >>
			}
			else
			{
				pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_FAILED());
				(void)pecallMgr->peCallAudio()->play_audio_with_language("010");
				ecall_trans_state(eCallState::ECALL_WAITING_FOR_CALLBACK);	
			}
			break;
		}
		//>> [LE0WNC-9450]
		//>> [LE0WNC-3099]
		default:
            break;
	}
}

void CeCallMachine::ecall_msd_urc_status(const uint32_t state)
{
    switch (state)
	{
        case static_cast<uint32_t>(FIH_ECALL_SENDING_START):
		{
            _logger.notice("FIH_ECALL_SENDING_START");
			m_msd_state = eCallMsdState::ECALL_MSD_SENDING_START;
			pecallMgr->peCallEventLog()->notifyUpdateEvent(MSD_TRANSMISSION_STATE, convert_u32(m_msd_state));
			//<< [LE0-10138] TracyTu, eCall trace develop (phase 5) 
			setMsdSendType(BBS_MSD_SEND_DEFAULT);  
			pecallMgr->peCallTrace()->setBBSMsdTag(static_cast<uint8_t>(m_msd_state));
			//>> [LE0-10138] 
			ecall_trans_state(eCallState::ECALL_MSD_TRANSFER); /*<<[LE0-7984] yusanhsu>>*/
			break;
		}
		case static_cast<uint32_t>(FIH_ECALL_SENDING_MSD):
		{
            _logger.notice("FIH_ECALL_SENDING_MSD");
			m_msd_state = eCallMsdState::ECALL_MSD_SENDING_MSD;
			pecallMgr->peCallEventLog()->notifyUpdateEvent(MSD_TRANSMISSION_STATE, convert_u32(m_msd_state)); 
			pecallMgr->peCallTrace()->setBBSMsdTag(static_cast<uint8_t>(m_msd_state)); //<< [LE0-10138] TracyTu, eCall trace develop (phase 5)  >>
            ecall_trans_state(eCallState::ECALL_MSD_TRANSFER);
			break;
		}
		case static_cast<uint32_t>(FIH_ECALL_LLACK_RECEIVED):
		{
            _logger.notice("FIH_ECALL_LLACK_RECEIVED");
			m_msd_state = eCallMsdState::ECALL_MSD_SENDING_LLACK_RECEIVED;
			pecallMgr->peCallEventLog()->notifyUpdateEvent(MSD_TRANSMISSION_STATE, convert_u32(m_msd_state)); 
			pecallMgr->peCallTrace()->setBBSMsdTag(static_cast<uint8_t>(m_msd_state)); //<< [LE0-10138] TracyTu, eCall trace develop (phase 5)  >>
            ecall_trans_state(eCallState::ECALL_MSD_TRANSFER);
			break;
		}
		case  static_cast<uint32_t>(FIH_ECALL_ALACK_POSITIVE_RECEIVED):
		{
            _logger.notice(" FIH_ECALL_ALACK_POSITIVE_RECEIVED");
			m_msd_state = eCallMsdState::ECALL_MSD_SENDING_ALACK_POSITION_RECEIVED;
			pecallMgr->peCallEventLog()->notifyUpdateEvent(MSD_TRANSMISSION_STATE, convert_u32(m_msd_state)); 
            // << [LE0WNC-3009] 20230207 ZelosZSCao: erase MSD log file after sending ack, REQ-0315113 A
            pecallMgr->peCallMSD()->remvoe_msd_log();
            _logger.information("ECALL MSD log stop timer");
            m_msdExpreTimer.stop();
			setEcallDataLogFileUpdateTime(INVALID_DATA_LOGFILE_TIME_SEC); // << [LE0-13446] TracyTu, delete MSD data log file >>
            // >> [LE0WNC-3009]
            _logger.imp("%s", fmt::format("[AT] start trans_state to ECALL_VOICE_COMMUNICATION: out call, ECALL_ALACK_POSITIVE_RECEIVED")); // << [LE022-5019][LE0WNC-9171] 20240925 EasonCCLiao >>
			pecallMgr->peCallTrace()->setBBSMsdTag(static_cast<uint8_t>(m_msd_state)); //<< [LE0-10138] TracyTu, eCall trace develop (phase 5)  >>
			ecall_trans_state(eCallState::ECALL_VOICE_COMMUNICATION);	
			break;
		}
		case static_cast<uint32_t>(FIH_ECALL_ALACK_CLEARDOWN_RECEIVED):
		{
            _logger.notice("FIH_ECALL_ALACK_CLEARDOWN_RECEIVED");
			
			pecallMgr->peCallFunction()->ecall_reset_ivs();
			pecallMgr->peCallFunction()->ecall_HangUp();

			pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_FINISHED());
			(void)pecallMgr->peCallAudio()->play_audio_with_language("011");
				
			m_msd_state = eCallMsdState::ECALL_MSD_SENDING_ALACK_CLEARDOWN_RECEIVED;
			pecallMgr->peCallEventLog()->notifyUpdateEvent(MSD_TRANSMISSION_STATE, convert_u32(m_msd_state)); 

            pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(eCallSubState::ECALL_SUB_DISCONNECTED)); 
			setEcallCallSubState(eCallSubState::ECALL_SUB_NONE);
			pecallMgr->peCallTrace()->setBBSMsdTag(static_cast<uint8_t>(m_msd_state)); //<< [LE0-10138] TracyTu, eCall trace develop (phase 5)  >>
			ecall_trans_state(eCallState::ECALL_WAITING_FOR_CALLBACK);	
			break;
		}
		case static_cast<uint32_t>(FIH_ECALL_DISCONNECTED):
		{
            _logger.notice("FIH_ECALL_DISCONNECTED");

			//<< [LE0WNC-9450] TracyTu, adjust redial strategy 
			m_statetimer.stop();
			setEcallCallSubState(eCallSubState::ECALL_SUB_DISCONNECTED);
			pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(getEcallCallSubState())); 
		
			_logger.information("%s", fmt::format("[ecall_msd_urc_status] m_msd_state:{}", m_msd_state));
			if(m_msd_state < eCallMsdState::ECALL_MSD_SENDING_ALACK_POSITION_RECEIVED)
			{
				pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_FAILED());
				pecallMgr->peCallHandler()->putStringToStateQ(ECALL_LED_GREEN_FIXED_LIGHT, std::string(""));
					
				if(!tryRedial())
				{	
					(void)pecallMgr->peCallAudio()->play_audio_with_language("010");
					ecall_trans_state(eCallState::ECALL_WAITING_FOR_CALLBACK);
				}			
			}
			else
			{
				pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_FINISHED());
				(void)pecallMgr->peCallAudio()->play_audio_with_language("011");
				ecall_trans_state(eCallState::ECALL_WAITING_FOR_CALLBACK);
			}
			//>> [LE0WNC-9450] 
			break;
		}
		//<< [LE0-8803] TracyTu
		case static_cast<uint32_t>(FIH_ECALL_ABNORMAL_HANGUP):
		{
			_logger.notice("FIH_ECALL_ABNORMAL_HANGUP:in msd transfer state");
			m_statetimer.stop();
			setEcallCallSubState(eCallSubState::ECALL_SUB_ABNORMAL_HANGUP);
			pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(getEcallCallSubState()));
		    		
			_logger.information("%s", fmt::format("[ecall_msd_urc_status] m_msd_state:{}", m_msd_state));
			if(m_msd_state < eCallMsdState::ECALL_MSD_SENDING_ALACK_POSITION_RECEIVED)
			{
				pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_FAILED());
				pecallMgr->peCallHandler()->putStringToStateQ(ECALL_LED_GREEN_FIXED_LIGHT, std::string(""));
					
				if(!tryRedial())
				{	
					(void)pecallMgr->peCallAudio()->play_audio_with_language("010");
					ecall_trans_state(eCallState::ECALL_WAITING_FOR_CALLBACK);
				}			
			}
			else
			{
				pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_FINISHED());
				(void)pecallMgr->peCallAudio()->play_audio_with_language("011");
				ecall_trans_state(eCallState::ECALL_WAITING_FOR_CALLBACK);
			}
			break;
		}
		//>> [LE0-8803]
		default:
            break;
	}
}

//<< [LE0WNC-9450] TracyTu, adjust redial strategy 
void CeCallMachine::ecall_redial_timeout(Poco::Timer&)
{
	_logger.notice("%s",fmt::format("ecall_redial_timeout, state in:{} ", getEcallMachineState()));
	pecallMgr->peCallHandler()->putStringToStateQ(ECALL_FLOW_REDIAL_TIMEOUT, std::string(""));
}
//>> [LE0WNC-9450]

void CeCallMachine::ecall_call_timeout(Poco::Timer&)
{
	if( eCallState::ECALL_CALL == getEcallMachineState() )
	{
		_logger.notice("ecall_call_timeout  ");
		pecallMgr->peCallHandler()->putStringToStateQ(ECALL_FLOW_CALLOUT_TIMEOUT, std::string(""));
	}
	else
	{
		_logger.warning("ecall_call_timeout on wrong state: {} ", getEcallMachineState() );
	}
}
void CeCallMachine::ecall_call_urc_status(const uint32_t state)
{
    switch (state)
	{
		case static_cast<uint32_t>(FIH_ECALL_ACTIVE):
		{
            _logger.notice("FIH_ECALL_ACTIVE");
			pecallMgr->peCallTrace()->setBBSSingleTag(BBS_ECALL_VOICE_STATE, BBS_VOICE_PICKUP); //<< [LE0-10138] TracyTu, eCall trace develop >>
			m_statetimer.stop(); //<< [LE0WNC-9450] TracyTu, adjust redial strategy >>
			setEcallCallSubState(eCallSubState::ECALL_SUB_ACTIVE);
			pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(getEcallCallSubState()));
			pecallMgr->peCallHandler()->putStringToStateQ(ECALL_LED_GREEN_BLINKING, std::string(""));
			m_msd_state = eCallMsdState::ECALL_MSD_NONE;
            ecall_trans_state(eCallState::ECALL_MSD_TRANSFER);
			break;
		}
		case static_cast<uint32_t>(FIH_ECALL_ABNORMAL_HANGUP):
		{
            _logger.notice("FIH_ECALL_ABNORMAL_HANGUP:in ECALL_CALL state");
			m_statetimer.stop(); //<< [LE0WNC-9450] TracyTu, adjust redial strategy >>
			setEcallCallSubState(eCallSubState::ECALL_SUB_ABNORMAL_HANGUP);
			pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(getEcallCallSubState()));
            //<< [LE0WNC-3099] TracyTu, Add abnormal hangup timeout in ECALL_CALL state
			//<< [LE0WNC-9450] TracyTu, adjust redial strategy
			pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_FAILED());
			
			if(!tryRedial())
			{
				m_prompttimer.stop();
				(void)pecallMgr->peCallAudio()->play_audio_with_language("010");
				ecall_trans_state(eCallState::ECALL_WAITING_FOR_CALLBACK);				
			}
			//>> [LE0WNC-9450] 
			//>> [LE0WNC-3099]
			break;
		}
		//<< [LE0WNC-9450] TracyTu, adjust redial strategy
		case static_cast<uint32_t>(FIH_ECALL_DISCONNECTED):
		{
            _logger.notice("FIH_ECALL_DISCONNECTED");
			if(isECallActive())
			{
				m_statetimer.stop();
				setEcallCallSubState(eCallSubState::ECALL_SUB_DISCONNECTED);
				pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(eCallSubState::ECALL_SUB_DISCONNECTED));
				
				pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_FAILED());
				
				if(!tryRedial())
				{
					m_prompttimer.stop();
					(void)pecallMgr->peCallAudio()->play_audio_with_language("010");
					ecall_trans_state(eCallState::ECALL_WAITING_FOR_CALLBACK);			
				}	
			}
			break;
		}
		//>> [LE0WNC-9450] 
		default:
            break;
	}
}

uint8_t CeCallMachine::ecall_get_call_state(const int32_t callId, const uint8_t state)
{
	/*<<[LE0-7984] yusanhsu*/
	uint8_t ret = static_cast<uint8_t>(FIH_ECALL_VOICE_STATE_NONE);
	m_callId = callId;
	ret = state;
	/*>>[LE0-7984] yusanhsu*/
	return ret;
}
// << [LE0-10138] 20231226 CK
void CeCallMachine::ecallDelayGetSignalStrength(Poco::Timer&) {
    _logger.notice("%s", fmt::format("ecallDelayGetSignalStrength"));
    pecallMgr->peCallFunction()->ecall_cellularnetwork_GetSignalStrength();
}
// >> [LE0-10138]

void CeCallMachine::ecall_process_call(const CeCallState::Ptr peCallState)
{
	switch (peCallState->getId())
	{
        // << [LE0-10138] 20231226 CK
        case ECALL_FLOW_REGISTRATION_SUCCESS: {
            _logger.notice("%s", fmt::format("ECALL_FLOW_REGISTRATION_SUCCESS: call state: {}, call sub state: {}", getEcallMachineState(), getEcallCallSubState()));
            m_getSignalStrengthTimer.stop();
            m_getSignalStrengthTimer.setStartInterval(2 * 1000);
            m_getSignalStrengthTimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecallDelayGetSignalStrength), "ecall_signal_timer");
            break;
        }
        // >> [LE0-10138]
        case ECALL_FLOW_URC_CALL_STATUS_IND:
		{
			_logger.notice("ECALL_FLOW_URC_CALL_STATUS_IND");
			const CeCallFunction::sCallState callState = std::any_cast<CeCallFunction::sCallState>(peCallState->getAny());
	        (void)ecall_get_call_state(callState.getCallId(), callState.getCallState());
            //<< [LE0-10138] TracyTu, eCall trace develop
			if(static_cast<uint8_t>(FIH_ECALL_VOICE_STATE_END) == callState.getCallState())
			{
				pecallMgr->peCallTrace()->setBBSHangupTag(callState.getDiscCause());
			} else if(static_cast<uint8_t>(FIH_ECALL_VOICE_STATE_ORIGINATING) == callState.getCallState())
			{
				pecallMgr->peCallTrace()->setBBSSingleTag(BBS_ECALL_VOICE_STATE, BBS_VOICE_ORIGINATING);
			} else if(static_cast<uint8_t>(FIH_ECALL_VOICE_STATE_ALERTING) == callState.getCallState())
			{
				pecallMgr->peCallTrace()->setBBSSingleTag(BBS_ECALL_VOICE_STATE, BBS_VOICE_ALERTING); 
			} else 
			{
				//do nothing
			}
			//>> [LE0-10138]
			break;
		}
		case ECALL_FLOW_URC_STATUS_IND:
		{
			_logger.notice("ECALL_FLOW_URC_STATUS_IND");
			const CeCallFunction::sEcallInfo eCallInfo = std::any_cast<CeCallFunction::sEcallInfo>(peCallState->getAny());
			ecall_call_urc_status(eCallInfo.state);
			break;
		}
		case ECALL_FLOW_CALLOUT_SUCCESS:
		{
            _logger.notice("ECALL_FLOW_CALLOUT_SUCCESS");
			pecallMgr->peCallRemoteServer()->nofityECallDialStatus(0U); //<<[LE0-7565] yusanhsu, 2023/08/24 >>
			break;
		}
		case ECALL_FLOW_CALLOUT_FAIL:
		{
            _logger.warning("ECALL_FLOW_CALLOUT_FAIL");
			setEcallCallSubState(eCallSubState::ECALL_SUB_FAIL);
			pecallMgr->peCallRemoteServer()->nofityECallDialStatus(1U); //<<[LE0-7565] yusanhsu, 2023/08/24 >>
			pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(getEcallCallSubState())); 
			pecallMgr->peCallTrace()->setBBSSingleTag(BBS_ECALL_VOICE_STATE, BBS_VOICE_FAILED); //<< [LE0-10138] TracyTu, eCall trace develop >>			
			break;
		}
		//<< [LE0WNC-9450]  
		case ECALL_FLOW_REDIAL_TIMEOUT:
		{
            _logger.imp("%s", fmt::format("[AT] ECALL_FLOW_REDIAL_TIMEOUT: call state: {}, call sub state: {}", getEcallMachineState(), getEcallCallSubState())); // << [LE022-5019] 20240925 EasonCCLiao >>
			if(!isECallActive())
            {		
				setEcallCallSubState(eCallSubState::ECALL_SUB_REDIAL);
				ecall_trans_state(eCallState::ECALL_CALL);
			}
			else
			{
				_logger.error("%s", fmt::format("One call is active, can't redial."));
			}
			break;
		}
		//>> [LE0WNC-9450] 
		case ECALL_FLOW_HANGUP_TIMEOUT:
		{
            _logger.notice("ECALL_FLOW_HANGUP_TIMEOUT");
			pecallMgr->peCallFunction()->ecall_HangUp();
			break;
		}
		//<< [LE0WNC-9450] TracyTu, adjust redial strategy
		case ECALL_FLOW_CALLOUT_TIMEOUT:
		{
            _logger.notice("ECALL_FLOW_CALLOUT_TIMEOUT");	
			pecallMgr->peCallTrace()->setBBSSingleTag(BBS_ECALL_VOICE_STATE, BBS_VOICE_TIMEOUT); //<< [LE0-10138] TracyTu, eCall trace develop >>			
			setEcallCallSubState(eCallSubState::ECALL_SUB_FAIL); 
			pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(getEcallCallSubState())); 
			
			pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_FAILED());
            pecallMgr->peCallFunction()->ecall_HangUp();

			if(!tryRedial())
			{
				m_prompttimer.stop();
				(void)pecallMgr->peCallAudio()->play_audio_with_language("010");	
            	ecall_trans_state(eCallState::ECALL_WAITING_FOR_CALLBACK);
			}
			break;
		}
		//>> [LE0WNC-9450]
		//<< [LE0WNC-2771]TracyTu, Add for demoapp restart
		case ECALL_FLOW_URC_DEMOAPP_STATUS_IND:
		{
			_logger.notice("%s", fmt::format("ECALL_FLOW_URC_DEMOAPP_STATUS_IND: call state: {}", getEcallMachineState())); //<< [LE0-9791] TracyTu, handle demoapp crash ind in call >>
			pecallMgr->peCallTrace()->setBBSSingleTag(BBS_ECALL_DEMO_APP_CRASH, 1U); //<< [LE0-10138] TracyTu, eCall trace develop (phase 4) >>
			m_statetimer.stop();
			setEcallCallSubState(eCallSubState::ECALL_SUB_ABNORMAL_HANGUP);
			pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(getEcallCallSubState()));
			pecallMgr->peCallFunction()->ecall_HangUp();

			pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_FAILED());
			
			if(!tryRedial())
			{
				m_prompttimer.stop();
				(void)pecallMgr->peCallAudio()->play_audio_with_language("010");
				ecall_trans_state(eCallState::ECALL_WAITING_FOR_CALLBACK);				
			}
			break;	
		}
		//>> [LE0WNC-2771]		
		default:
			break;
	}
}
void CeCallMachine::ecall_msd_transfer_timeout(Poco::Timer&)
{
	m_msd_state = eCallMsdState::ECALL_MSD_SENDING_TIMEOUT; //<< [LE0-10138] TracyTu, eCall trace develop (phase 5) >>
	if( (eCallState::ECALL_MSD_TRANSFER == getEcallMachineState()) || (eCallState::ECALL_VOICE_COMMUNICATION == getEcallMachineState()) )  // << [LE0-15382] TracyTu, Add msd state when psap request >>
	{
		_logger.notice("ecall_msd_transfer_timeout");
		pecallMgr->peCallHandler()->putStringToStateQ(ECALL_FLOW_MSD_TRANSFER_TIMEOUT, std::string(""));
	}
	else
	{
		_logger.warning("ecall_msd_transfer_timeout on wrong state: {} ", getEcallMachineState() );
	}
}
void CeCallMachine::ecall_process_msd_transfer(const CeCallState::Ptr peCallState)
{
	switch (peCallState->getId())
	{
		case ECALL_FLOW_MSD_TRANSFER_TIMEOUT:
		{
			_logger.notice("ECALL_FLOW_MSD_TRANSFER_TIMEOUT in ECALL_MSD_TRANSFER"); // << [LE0-15382] TracyTu, Add msd state when psap request >>
            _logger.imp("%s", fmt::format("[AT] start trans_state to ECALL_VOICE_COMMUNICATION: out call, ECALL_FLOW_MSD_TRANSFER_TIMEOUT")); // << [LE022-5019][LE0WNC-9171] 20240925 EasonCCLiao >>
            pecallMgr->peCallTrace()->setBBSMsdTag(static_cast<uint8_t>(m_msd_state)); //<< [LE0-10138] TracyTu, eCall trace develop (phase 5)  >>
			ecall_trans_state(eCallState::ECALL_VOICE_COMMUNICATION);			
			break;
		}
		case ECALL_FLOW_URC_STATUS_IND:
		{
			_logger.notice("ECALL_FLOW_URC_STATUS_IND");
			const CeCallFunction::sEcallInfo eCallInfo = std::any_cast<CeCallFunction::sEcallInfo>(peCallState->getAny());
			ecall_msd_urc_status(eCallInfo.state);
			break;
		}
		case ECALL_FLOW_URC_CALL_STATUS_IND:
		{
			_logger.notice("ECALL_FLOW_URC_CALL_STATUS_IND");
			const CeCallFunction::sCallState callState = std::any_cast<CeCallFunction::sCallState>(peCallState->getAny());
			/* <<[LE0-7984] yusanhsu*/
	        uint8_t call_state = ecall_get_call_state(callState.getCallId(), callState.getCallState());
			_logger.notice("%s",fmt::format("ecall_process_msd_transfer stop timer ECALL_FLOW_URC_CALL_STATUS_IND {}",call_state));
			if(static_cast<uint8_t>(FIH_ECALL_VOICE_STATE_END) == call_state){
				m_statetimer.stop();				
				pecallMgr->peCallTrace()->setBBSHangupTag(callState.getDiscCause()); //<< [LE0-10138] TracyTu, eCall trace develop >>
			}
			/* >>[LE0-7984] yusanhsu*/
			break;
		}
		//<< [LE0WNC-9450] TracyTu, adjust redial strategy 	
		case ECALL_FLOW_REDIAL_TIMEOUT:
		{
            _logger.imp("%s", fmt::format("[AT] ECALL_FLOW_REDIAL_TIMEOUT: call state: {}, call sub state: {}", getEcallMachineState(), getEcallCallSubState())); // << [LE022-5019] 20240925 EasonCCLiao >>
			if(!isECallActive())
            {		
				setEcallCallSubState(eCallSubState::ECALL_SUB_REDIAL);
				ecall_trans_state(eCallState::ECALL_CALL);
			}
			else
			{
				_logger.error("%s", fmt::format("One call is active, can't redial."));
			}
			break;
		}
		//>> [LE0WNC-9450] 
		//<< [LE0WNC-2771]TracyTu, Add for demoapp restart
		case ECALL_FLOW_URC_DEMOAPP_STATUS_IND:
		{
			_logger.notice("%s", fmt::format("ECALL_FLOW_URC_DEMOAPP_STATUS_IND: call state: {}", getEcallMachineState())); //<< [LE0-9791] TracyTu, handle demoapp crash ind in call >>
			pecallMgr->peCallTrace()->setBBSSingleTag(BBS_ECALL_DEMO_APP_CRASH, 1U); //<< [LE0-10138] TracyTu, eCall trace develop (phase 4) >>
			m_statetimer.stop();
			setEcallCallSubState(eCallSubState::ECALL_SUB_ABNORMAL_HANGUP);
			pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(getEcallCallSubState()));
			pecallMgr->peCallFunction()->ecall_HangUp();

			pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_FAILED());
			pecallMgr->peCallHandler()->putStringToStateQ(ECALL_LED_GREEN_FIXED_LIGHT, std::string(""));
			
			if(!tryRedial())
			{
				(void)pecallMgr->peCallAudio()->play_audio_with_language("010");
				ecall_trans_state(eCallState::ECALL_WAITING_FOR_CALLBACK);				
			}
			break;
		}
		//>> [LE0WNC-2771]
		default:
			break;
	}
}
void CeCallMachine::ecall_clear_down_timeout(Poco::Timer&)
{
	if( eCallState::ECALL_VOICE_COMMUNICATION == getEcallMachineState() )
	{
		_logger.notice("ecall_clear_down_timeout  ");
		pecallMgr->peCallHandler()->putStringToStateQ(ECALL_FLOW_CLEARDOWN_TIMEOUT, std::string(""));

	}
	else
	{
		_logger.warning("ecall_clear_down_timeout on wrong state: {} ", getEcallMachineState() );
	}
}

//<< [LE0-6399] TracyTu, Play beep after pressing the button for two seconds
//[LE0-8913] yusanhsu
void CeCallMachine::ecall_beep_prompt_timeout(Poco::Timer&){
	_logger.notice("%s", fmt::format("ecall_beep_prompt_timeout play beep sound CallState:{},CallSubState:{}",getEcallMachineState(),getEcallCallSubState()));
	if((getEcallMachineState() == eCallState::ECALL_OFF) 
			|| ((pecallMgr->peCallMachine()->isWCBIdle()) && (getEcallCallSubState() != eCallSubState::ECALL_SUB_INCOMING))){ // << [LE0-15588] 20240823 MaoJunYan >>
		(void)pecallMgr->peCallAudio()->play_audio_with_language("001"); // << [LE0-7069] 20230904 CK >>
		m_is_prompt1_played = true;
	}
}
//>> [LE0-6399]

void CeCallMachine::ecall_ringtone_prompt_timeout(Poco::Timer&)
{
	_logger.information("ecall_ringtone_prompt_timeout");
	pecallMgr->peCallAudio()->playAudioWithLanguage004(); // << [LE0-6062] 20230630 CK >>
}

void CeCallMachine::ecall_registration_prompt_timeout(Poco::Timer&)
{
	_logger.information("ecall_registration_prompt_timeout");
	pecallMgr->peCallFunction()->ecall_cellularnetwork_GetVoiceRegistration(); // << [LE0-9791] 20231214 JasonKHLee >>
    pecallMgr->peCallAudio()->playAudioWithLanguage008(); // << [LE0-6210][LE0-6062][LE0-5724][LE0-5671] 20230621 CK >>
}
void CeCallMachine::ecall_callback_prompt_timeout(Poco::Timer&)
{
	_logger.notice("%s",fmt::format("ecall_callback_prompt_timeout time: {} ", call_back_time));
	pecallMgr->peCallRemoteServer()->notifyWaitingForCallbackTimer(call_back_time);
	call_back_time++;
}

// << [LE0WNC-3009] 20230207 ZelosZSCao
void CeCallMachine::ecall_callback_msd_log_expired(Poco::Timer&)
{
    _logger.notice("%s", fmt::format("ecall MSD log expired"));
    pecallMgr->peCallMSD()->remvoe_msd_log();
	setEcallDataLogFileUpdateTime(INVALID_DATA_LOGFILE_TIME_SEC);  // << [LE0-13446] TracyTu, delete MSD data log file >>
}
// >> [LE0WNC-3009]

void CeCallMachine::ecall_process_voice_comm(const CeCallState::Ptr peCallState)
{
	switch (peCallState->getId())
	{
		// << [LE0-15382] TracyTu, Add msd state when psap request
		case ECALL_FLOW_MSD_TRANSFER_TIMEOUT:
		{
			_logger.notice("ECALL_FLOW_MSD_TRANSFER_TIMEOUT in ECALL_VOICE_COMMUNICATION");
            pecallMgr->peCallTrace()->setBBSMsdTag(static_cast<uint8_t>(m_msd_state)); //<< [LE0-10138] TracyTu, eCall trace develop (phase 5)  >>
			pecallMgr->peCallFunction()->ecall_reset_ivs();	
			break;
		}
		// >> [LE0-15382] 
		case ECALL_FLOW_CLEARDOWN_TIMEOUT:
		{
			_logger.imp("[AT] ECALL_FLOW_CLEARDOWN_TIMEOUT"); // << [LE022-5019] 20240925 EasonCCLiao >>

			pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_FINISHED());

			pecallMgr->peCallFunction()->ecall_HangUp();

			pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(eCallSubState::ECALL_SUB_DISCONNECTED)); 
			setEcallCallSubState(eCallSubState::ECALL_SUB_NONE);
			
			(void)pecallMgr->peCallAudio()->play_audio_with_language("011");
			ecall_trans_state(eCallState::ECALL_WAITING_FOR_CALLBACK);			
			break;
		}
		case ECALL_FLOW_URC_STATUS_IND:
		{
			_logger.notice("ECALL_FLOW_URC_STATUS_IND");
			const CeCallFunction::sEcallInfo eCallInfo = std::any_cast<CeCallFunction::sEcallInfo>(peCallState->getAny());
			ecall_comm_urc_status(eCallInfo.state, eCallInfo.callid);
			break;
		}
		case ECALL_FLOW_URC_CALL_STATUS_IND:
		{
			_logger.notice("ECALL_FLOW_URC_CALL_STATUS_IND");
			const CeCallFunction::sCallState callState = std::any_cast<CeCallFunction::sCallState>(peCallState->getAny());
			uint8_t call_state = ecall_get_call_state(callState.getCallId(), callState.getCallState());
			_logger.notice( "%s", fmt::format("call_state:{} call_sub_state:{}", call_state, getEcallCallSubState()));
			//<< [LE0WNC-3099] TracyTu, prompt "9"didn't played
			//DO outgoing disconnect in ECALL_FLOW_URC_STATUS_IND
			if(static_cast<uint8_t>(FIH_ECALL_VOICE_STATE_END) == call_state)
			{
				pecallMgr->peCallTrace()->setBBSHangupTag(callState.getDiscCause()); //<< [LE0-10138] TracyTu, eCall trace develop >>
				if(0x02U == callState.getCallDirection())
				{
					if(eCallSubState::ECALL_SUB_ACTIVE == getEcallCallSubState())
					{
						pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_FINISHED());

						pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(eCallSubState::ECALL_SUB_DISCONNECTED)); 
						setEcallCallSubState(eCallSubState::ECALL_SUB_NONE);
						(void)pecallMgr->peCallAudio()->play_audio_with_language("011");
						ecall_trans_state(eCallState::ECALL_WAITING_FOR_CALLBACK);	
					}
				}
			} 
			//>> [LE0WNC-3099] 
			break;
		}
		//<< [LE0WNC-9450] TracyTu, adjust redial strategy 	
		case ECALL_FLOW_REDIAL_TIMEOUT:
		{
            _logger.imp("%s", fmt::format("[AT] ECALL_FLOW_REDIAL_TIMEOUT: call state: {}, call sub state: {}", getEcallMachineState(), getEcallCallSubState())); // << [LE022-5019] 20240925 EasonCCLiao >>
			if(!isECallActive())
            {		
				setEcallCallSubState(eCallSubState::ECALL_SUB_REDIAL);
				ecall_trans_state(eCallState::ECALL_CALL);
			}
			else
			{
				_logger.error("%s", fmt::format("One call is active, can't redial."));
			}
			break;
		}
		//>> [LE0WNC-9450] 
		//<< [LE0WNC-2771]TracyTu, Add for demoapp restart
		case ECALL_FLOW_URC_DEMOAPP_STATUS_IND:
		{
			_logger.notice("%s", fmt::format("ECALL_FLOW_URC_DEMOAPP_STATUS_IND: call state: {}", getEcallMachineState())); //<< [LE0-9791] TracyTu, handle demoapp crash ind in call >>
			pecallMgr->peCallTrace()->setBBSSingleTag(BBS_ECALL_DEMO_APP_CRASH, 1U); //<< [LE0-10138] TracyTu, eCall trace develop (phase 4) >>
			setEcallCallSubState(eCallSubState::ECALL_SUB_ABNORMAL_HANGUP);
			pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(getEcallCallSubState()));
			pecallMgr->peCallFunction()->ecall_HangUp();
			
			if(tryRedial())
			{
				pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_CALL_DROPPED());
				pecallMgr->peCallAudio()->playAudioWithLanguage009(); // << [LE0-7069] 20230904 CK >>
			}
			else
			{
				pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_FAILED());
				(void)pecallMgr->peCallAudio()->play_audio_with_language("010");
				ecall_trans_state(eCallState::ECALL_WAITING_FOR_CALLBACK);
			}
			break;
		}
		//>> [LE0WNC-2771]
		default:
			break;
	}
}
void CeCallMachine::ecall_process_wait_callback(const CeCallState::Ptr peCallState)
{
    _logger.notice("%s", fmt::format("ecall_process_wait_callback peCallState ID = {}, data = {}", peCallState->getId(), peCallState->getString())); // << [LE0-11009][LE0-11039][LE022-983] 20240229 CK >>
	uint8_t trigger_type = pecallMgr->peCallTrace()->convertBBSTriggerType(peCallState->getString()); //<< [LE0-10138] TracyTu, eCall trace develop >>
	switch (peCallState->getId())
	{
	    case ECALL_FLOW_BTN_LONG_PRESS:
		{
			_logger.notice("ECALL_FLOW_BTN_LONG_PRESS");
			pecallMgr->peCallTrace()->setBBSTriggerType(trigger_type); //<< [LE0-10138] TracyTu, eCall trace develop >>
			setXcallStatus(XcallStatus::MANUAL_ECALL_TRIGGERED);  //<< [V3-REQ] TracyTu, for xCall status >>
			processLongPressInEcallWCB();
			break;
		}
        case ECALL_FLOW_MANUAL_TRIGGER:
		{
			_logger.notice("ECALL_FLOW_MANUAL_TRIGGER");
			ecall_triggered_information_record(false); // << [LE0WNC-2410] 20230214 CK >>
			pecallMgr->peCallTrace()->setBBSTriggerType(trigger_type); //<< [LE0-10138] TracyTu, eCall trace develop >>
			setXcallStatus(XcallStatus::MANUAL_ECALL_TRIGGERED);  //<< [V3-REQ] TracyTu, for xCall status >>
			setExitWCBTime(); //<< [LE0-6152]] TracyTu, Recalculate WCB time when trigger new call in WCB >>
			ecall_trans_state(eCallState::ECALL_WCB_ECALL_MANUAL_TRIGGERED);
			break;
		}
		case ECALL_FLOW_AUTO_TRIGGER:
        case ECALL_FLOW_AIR_BAG_MESSAGE:
		{
			_logger.notice("ECALL_FLOW_AUTO_TRIGGER/ECALL_FLOW_AIR_BAG_MESSAGE");
			setEcallCallSubState(eCallSubState::ECALL_SUB_NONE);
			ecall_triggered_information_record(true); // << [LE0WNC-2410] 20230214 CK >>
			pecallMgr->peCallTrace()->setBBSTriggerType(trigger_type); //<< [LE0-10138] TracyTu, eCall trace develop >>
			setXcallStatus(XcallStatus::AUTO_ECALL_TRIGGERED);  //<< [V3-REQ] TracyTu, for xCall status >>
			pecallMgr->peCallAudio()->playAudioWithLanguage008();  //[LE0-6465] TracyTu, Play prompt 8 when do auto trigger
			ecall_trans_state(eCallState::ECALL_CALL); //REQ-0315093
			break;
		}
        case ECALL_FLOW_MANUAL_CANCEL:
		case ECALL_FLOW_BTN_SHORT_PRESS:
		{
			_logger.notice("ECALL_FLOW_MANUAL_CANCEL");
			if(eCallState::ECALL_WCB_ECALL_MANUAL_TRIGGERED == getEcallMachineSubState())
			{
				(void)pecallMgr->peCallAudio()->play_audio_with_language("007");
			    pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_CANCELED());
				ecall_trans_state(eCallState::ECALL_WCB_ECALL_IDLE);
			}
			break;
		}
        case ECALL_FLOW_MANUAL_CANCEL_TIMEOUT:
		{
			_logger.notice("ECALL_FLOW_MANUAL_CANCEL_TIMEOUT");
			setEcallCallSubState(eCallSubState::ECALL_SUB_NONE);
			ecall_trans_state(eCallState::ECALL_CALL);
			break;
		}
	    case ECALL_FLOW_CALL_BACK_TIMEOUT:
		{
            _logger.notice("%s", fmt::format("ECALL_FLOW_CALL_BACK_TIMEOUT: {}", getEcallMachineSubState()));		
            pecallMgr->peCallTrace()->setBBSWCBTimeout(); // << [LE0-10138] 20231225 CK >>
			// << [LE0-8731] TracyTu
            if((eCallState::ECALL_WCB_ECALL_IDLE == getEcallMachineSubState()) && (eCallSubState::ECALL_SUB_INCOMING != getEcallCallSubState())){ //<<[LE0-7329] yusanhsu
                ecall_trans_state(eCallState::ECALL_OFF);
                pecallMgr->peCallHandler()->putStringToStateQ(ECALL_FLOW_FINISHED, std::string(""));
            } //>>[LE0-7329]
			else {
				_logger.error("%s", fmt::format("skip BCALL_FLOW_CALL_BACK_TIMEOUT sub_state:{} call_sub_status:{}", m_sub_state, m_call_sub_state));
			}
			// >> [LE0-8731]
			break;
		}
		case ECALL_DIAG_REQ_CANCEL_ECALL:
		{
			_logger.notice("ECALL_DIAG_REQ_CANCEL_ECALL");
			if(eCallState::ECALL_WCB_ECALL_IDLE == getEcallMachineSubState())
			{
			    pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_CANCELED());
				ecall_trans_state(eCallState::ECALL_OFF);
    			pecallMgr->peCallHandler()->putStringToStateQ(ECALL_FLOW_FINISHED, std::string(""));
			}
			break;
		}
		case ECALL_FLOW_URC_CALL_STATUS_IND:
		{
			_logger.notice("ECALL_FLOW_URC_CALL_STATUS_IND");
			const CeCallFunction::sCallState callState = std::any_cast<CeCallFunction::sCallState>(peCallState->getAny());
			processIncomingCallInEcallWCB(callState);			
			break;
		}
		//<< [LE0WNC-3099] TracyTu, prompt "9"didn't played
		case ECALL_FLOW_REDIAL_TIMEOUT:
		{
			_logger.imp("%s", fmt::format("[AT] ECALL_FLOW_REDIAL_TIMEOUT: call state: {}, call sub state: {}", getEcallMachineState(), getEcallCallSubState())); // << [LE022-5019] 20240925 EasonCCLiao >>
			//<< [LE0WNC-9450] TracyTu, adjust redial strategy 	
			if(!isECallActive())
            {   
				pecallMgr->pCmdThread()->sendstringcmd(static_cast<uint32_t>(AUDIOMANAGER_TYPE), static_cast<uint32_t>(AUDIO_GPIO_UNMUTE), static_cast<uint16_t>(NORMAL), std::string(""));
				pecallMgr->peCallHandler()->putStringToStateQ(ECALL_LED_GREEN_FIXED_LIGHT, std::string(""));
			    setEcallCallSubState(eCallSubState::ECALL_SUB_REDIAL);
				ecall_trans_state(eCallState::ECALL_CALL);
			}
			else
			{
				_logger.error("%s", fmt::format("One call is active, can't redial."));
			}
			//>> [LE0WNC-9450]
			break;
		}
		//>> [LE0WNC-3099] 
		//<< [LE0-9791] TracyTu, handle demoapp crash ind in call 
		case ECALL_FLOW_URC_DEMOAPP_STATUS_IND:
		{
			pecallMgr->peCallTrace()->setBBSSingleTag(BBS_ECALL_DEMO_APP_CRASH, 1U); //<< [LE0-10138] TracyTu, eCall trace develop (phase 4) >>
			_logger.notice("%s", fmt::format("ECALL_FLOW_URC_DEMOAPP_STATUS_IND: call state: {}", getEcallMachineState()));
			break;
		}
		//>> [LE0-9791]
		default:
			break;
	}
}
void CeCallMachine::ecall_process_cmd(const CeCallState::Ptr peCallState)
{
	_logger.imp("%s", fmt::format("[AT] current call state: {}", getEcallMachineState())); // << [LE022-5019] 20240925 EasonCCLiao >>

	switch (getEcallMachineState())
	{
		case eCallState::ECALL_OFF:
		{
			ecall_process_off(peCallState);
			break;
		}
		case eCallState::ECALL_MANUAL_TRIGGERED:
		{
			ecall_process_manual_trigger(peCallState);
			break;
		}
		case eCallState::ECALL_REGISTRATION:
		{
			ecall_process_registration(peCallState);
			break;
		}
		case eCallState::ECALL_CALL:
		{
			ecall_process_call(peCallState);
			break;
		}
        case eCallState::ECALL_MSD_TRANSFER:
		{
			ecall_process_msd_transfer(peCallState);
			break;
		}
        case eCallState::ECALL_VOICE_COMMUNICATION:
		{
			ecall_process_voice_comm(peCallState);
			break;
		}
        case eCallState::ECALL_WAITING_FOR_CALLBACK:
		{
			ecall_process_wait_callback(peCallState);
			break;
		}
		default:
            break;
	}
}

uint32_t CeCallMachine::convert_u32(eCallSubState call_sub_state)
{   
	_logger.information("%s", fmt::format("[convert_u32] call_sub_state: {}", call_sub_state));
    
	uint32_t state = 0U;
    switch (call_sub_state)
	{
	    case eCallSubState::ECALL_SUB_NONE:
			state = 0U;
			break;
		case eCallSubState::ECALL_SUB_FAIL:
			state = 1U;
			break;
		case eCallSubState::ECALL_SUB_ABNORMAL_HANGUP:
		    state = 2U;
			break;
		case eCallSubState::ECALL_SUB_ACTIVE:
			state = 3U;
			break;
		case eCallSubState::ECALL_SUB_INCOMING:
			state = 4U;
			break;
		case eCallSubState::ECALL_SUB_DIAL:
			state = 5U;
			break;
		case eCallSubState::ECALL_SUB_DISCONNECTED:
			state = 2U;
			break;
	    default:
			state = 0U;
            break;
	}

	return state;

}

uint32_t CeCallMachine:: convert_u32(eCallMsdState msd_state)
{   
	_logger.information("%s", fmt::format("[convert_u32] msd_state: {}", msd_state)); 
    uint32_t state = 0U;
    switch (msd_state)
	{
	    case eCallMsdState::ECALL_MSD_NONE:
			state = 3U;
			break;
		case eCallMsdState::ECALL_MSD_SENDING_START:
			state = 2U;
			break;
		case eCallMsdState::ECALL_MSD_SENDING_MSD:
		    state = 0U;
			break;
		case eCallMsdState::ECALL_MSD_SENDING_LLACK_RECEIVED:
			state = 3U;
			break;
		case eCallMsdState::ECALL_MSD_SENDING_ALACK_POSITION_RECEIVED:
			state = 1U;
			break;
		case eCallMsdState::ECALL_MSD_SENDING_ALACK_CLEARDOWN_RECEIVED:
			state = 1U;
			break;
	    default:
			state = 0U;
            break;
	}

	return state;

}

void  CeCallMachine:: ecall_make_fast_ecall()
{
	std::string ecall_number = pecallMgr->peCallMachine()->getEcallNumber();
	//<< [REQ-0314811]TracyTu, Adjust TYPE_EMERGENCY_CALL
    pecallMgr->peCallMSD()->set_data_test_call(false); // << [LE0-9841] 20231207 CK >>

	_logger.notice("%s",fmt::format("ecall_number.size:{} ", ecall_number.size()));
	if(ecall_number.size() > 0U)
	{
		pecallMgr->peCallMSD()->set_data_test_call(true);
	}
	//>> [REQ-0314811]

	_logger.notice("%s",fmt::format("m_diag_test_ecall_session:{} ", m_diag_test_ecall_session));
	if(true == m_diag_test_ecall_session)
	{
        pecallMgr->peCallMSD()->set_data_test_call(true); // << [LE0-9841] 20231207 CK >>
		pecallMgr->peCallConfig()->update_EcallType_EmegrPr();
		ecall_number = pecallMgr->peCallMachine()->getEcallTestNumber();
	}

    // << [LE0-11581] 20240215 CK
    bool isEmergencyECallWhiteListNumber = false;
    _logger.notice("%s",fmt::format("ecall_number:{} ", ecall_number));
    if (isEcallTypePE112() && (ecall_number.compare("112") == 0)) {
        isEmergencyECallWhiteListNumber = true;
    }

    _logger.notice("%s", fmt::format("isEmergencyECallWhiteListNumber:{} ", isEmergencyECallWhiteListNumber));
    if (isEmergencyECallWhiteListNumber) {
        pecallMgr->peCallMSD()->set_data_test_call(false);
    }
    // >> [LE0-11581]

	FIH_FAST_ECALL_Variant_E  ecall_mode = FIH_FAST_ECALL_TEST;
	_logger.notice("%s",fmt::format("test_call:{} ", pecallMgr->peCallMSD()->get_msd_data().test_call));
	if(false == pecallMgr->peCallMSD()->get_msd_data().test_call)
	{
		ecall_number = "null";
		ecall_mode  = FIH_FAST_ECALL_EMERGENCY;
	}

	FIH_FAST_ECALL_Category_E ecall_cat = FIH_FAST_ECALL_AUTO_ECALL;
	_logger.notice("%s",fmt::format("automatic_activation:{} ", pecallMgr->peCallMSD()->get_msd_data().automatic_activation));
	if(false == pecallMgr->peCallMSD()->get_msd_data().automatic_activation)
	{
		ecall_cat = FIH_FAST_ECALL_MANUAL_ECALL;
	}

	pecallMgr->peCallFunction()->ecall_make_fast_ecall(ecall_cat, 
												ecall_mode, 
												m_diag_test_ecall_session,
												ecall_number);

	pecallMgr->peCallEventLog()->notifyUpdateEvent(ECALL_SESSION_STATE, ECALL_SESSION_STATE_STARTED, ecall_mode, ecall_number);
}

void CeCallMachine::notify(const uint32_t event, const std::string data)
{
    switch (event)
	{
        case XCALL_VEHICLE_BATT_NOTIFY:
		{
			setVehicleBattery(data); //<< [LE0-10138] JasonKHLee, eCall trace develop >>
			if (getVehicleBattery() != "lost") {//<< [LE0-10138] JasonKHLee, eCall trace develop >>
				setPowerSource(BBS_VEHICLE); //<< [LE0-10138] TracyTu, eCall trace develop >>
			}
			std::string pre_audio_device = ecall_get_audio_device();
			_logger.notice("%s",fmt::format("notify XCALL_VEHICLE_BATT_NOTIFY pre_audio_device:{} data:{} ", pre_audio_device, data));//<< [LE0-10138] JasonKHLee, eCall trace develop >>
            ecall_set_audio_device((getVehicleBattery() != "lost")? "HANDSFREE" : "TBOX_HANDSFREE");

            if(pre_audio_device != ecall_get_audio_device()) // << [LE0-7756] 20230829 CK
            {
                pecallMgr->peCallAudio()->set_audio_device(ecall_get_audio_device());
            }
            break;
        }
		case XCALL_BACKUP_BATT_NOTIFY: 
		{
			setBackupBattery(data); //<< [LE0-10138] JasonKHLee, eCall trace develop >>
			if (getVehicleBattery() == "lost" && getBackupBattery() != "lost") { //<< [LE0-10138] JasonKHLee, eCall trace develop >>
				setPowerSource(BBS_BACKUP); //<< [LE0-10138] TracyTu, eCall trace develop >>
			}
			std::string pre_audio_device = ecall_get_audio_device(); 
			_logger.notice("%s", fmt::format("notify XCALL_BACKUP_BATT_NOTIFY pre_audio_device:{} data:{} ", pre_audio_device, data));//<< [LE0-10138] JasonKHLee, eCall trace develop >>
            ecall_set_audio_device((getVehicleBattery()=="lost" && getBackupBattery() != "lost") ? "TBOX_HANDSFREE" : "HANDSFREE");
			
			if(pre_audio_device != ecall_get_audio_device()) // << [LE0-7756] 20230829 CK
			{
				pecallMgr->peCallAudio()->set_audio_device(ecall_get_audio_device());
			}
			break;
		}
        default:
            break;
    }
}

void  CeCallMachine::ecall_set_audio_device(std::string device)
{
	_logger.notice("%s",fmt::format("ecall_set_audio_device device:{} ", device));
	pecallMgr->peCallMachine()->setAudioDevice(device); // << [LE0-7756] 20230829 CK
}

std::string CeCallMachine::ecall_get_audio_device()
{   
	_logger.notice("%s",fmt::format("ecall_get_audio_device device:{} ", pecallMgr->peCallMachine()->getAudioDevice()));
	return pecallMgr->peCallMachine()->getAudioDevice();
}

// << [LE0WNC-2616] 20230117 CK: save latest call triggered/button state, which may not be currently running Ecall/Acall
void CeCallMachine::setLatestECallTriggeredMethodId(const uint32_t newECallTriggeredMethodId) {
    // << [LE0-3217][RTBMVAL-506] 20230515 CK
    latestEcallTriggeredMethodId = newECallTriggeredMethodId;
    latestEcallStartedAt = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    pecallMgr->peCallXmlConfig()->saveLatestECallTriggeredMethodId(latestEcallTriggeredMethodId);
    pecallMgr->peCallXmlConfig()->saveLatestECallStartAt(latestEcallStartedAt);
    setLatestEcallTriggeredMethodIdToConfig(latestEcallTriggeredMethodId);
    setLatestEcallStartAt(latestEcallStartedAt);

    _logger.notice("%s", fmt::format("setLatestCallTriggeredMethodId latestEcallTriggeredMethodId:{}, latestEcallStartedAt:{}",
                                     latestEcallTriggeredMethodId, latestEcallStartedAt));
    // >> [LE0-3217][RTBMVAL-506]
}
void CeCallMachine::setEcallCurrentState(const uint32_t newEcallState) {
    ecall_current_state = newEcallState;
    update3A9StateV2(); // << [LE0-13731] 20240514 CK >>
}
void CeCallMachine::setAcallCurrentState(const uint32_t newAcallState) {
    bcall_current_state = newAcallState;
    update3A9StateV2(); // << [LE0-13731] 20240514 CK >>
	//<< [V3-REQ] TracyTu, for xCall status
	if(XCALL_STATE_ECALL_OFF == getEcallCurrentState()) 
	{
		if(XCALL_STATE_BCALL_TRIGGERED == bcall_current_state) 
		{
			setXcallStatus(XcallStatus::ACALL_ON);
		}
		else if(XCALL_STATE_BCALL_OFF == bcall_current_state)
		{
			setXcallStatus(XcallStatus::NO_XCALL);
		}
		else
		{
			_logger.information("Do not set XCALL_STATUS!");
		}		
	}
	//>> [V3-REQ] 
}
void CeCallMachine::onBtnStateChanged(uint32_t newBtnState) {
    bool isValidState = true;
    switch (newBtnState) {
        case XCALL_ECALL_BTN_PRESS:
            ecallBtnStateType = ButtonState_Type::SHORT_PRESSED;
            m_ecallBtnPressOver2secTimer.stop();
            m_ecallBtnPressOver2secTimer.setStartInterval(2 * 1000);
            m_ecallBtnPressOver2secTimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::setECallBtnStateTypeViaBtnPressOver2sec), "ecall_btn_press_timer");
			//<<[LE0-6399][LE0-8913]yusanhsu
			m_beepTimer.stop();
			m_beepTimer.setStartInterval(2 * 1000);
			m_beepTimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_beep_prompt_timeout), "ecall_beep_timer");
			//>>[LE0-6399][LE0-8913]
            pecallMgr->peFihSysApiInterface()->powerWakelockEnableECallBtnPressWait();
            break;
        case XCALL_ECALL_BTN_RELEASE:
            m_ecallBtnPressOver2secTimer.stop();
			m_beepTimer.stop(); //<<[LE0-6399][LE0-8913]yusanhsu>>
			break;
        case XCALL_ECALL_BTN_SHORT_PRESS:
            ecallBtnStateType = ButtonState_Type::NOT_PRESSED;
            pecallMgr->peFihSysApiInterface()->powerWakelockEnableECallBtnShortPress();
            break;
        case XCALL_ECALL_BTN_LONG_PRESS:
            ecallBtnStateType = ButtonState_Type::LONG_RELEASED;
            pecallMgr->peFihSysApiInterface()->powerWakelockEnableECallBtnLongPress();
            break;
        case XCALL_BCALL_BTN_PRESS:
            acallBtnStateType = ButtonState_Type::SHORT_PRESSED;
            m_acallBtnPressOver2secTimer.stop();
            m_acallBtnPressOver2secTimer.setStartInterval(2 * 1000);
            m_acallBtnPressOver2secTimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::setACallBtnStateTypeViaBtnPressOver2sec), "acall_btn_press_timer");
            break;
        case XCALL_BCALL_BTN_RELEASE:
            m_acallBtnPressOver2secTimer.stop();
            break;
        case XCALL_BCALL_BTN_SHORT_PRESS:
            acallBtnStateType = ButtonState_Type::NOT_PRESSED;
            break;
        case XCALL_BCALL_BTN_LONG_PRESS:
            acallBtnStateType = ButtonState_Type::LONG_RELEASED;
            break;
        default:
            _logger.error("%s", fmt::format("onBtnStateChanged, undefined newBtnState:{}", newBtnState));
            isValidState = false;
            break;
    }
    if (isValidState) { 
        _logger.notice("%s", fmt::format("onBtnStateChanged newBtnState:{}, ecallBtnStateType:{}, acallBtnStateType:{}",
                                     newBtnState, ecallBtnStateType, acallBtnStateType));
    } 
}
void CeCallMachine::setECallBtnStateTypeViaBtnPressOver2sec(Poco::Timer&) 
{
    if (ecallBtnStateType == ButtonState_Type::SHORT_PRESSED) {
		ecallBtnStateType = ButtonState_Type::LONG_PRESSED;
		_logger.notice("%s", fmt::format("setECallBtnStateTypeViaBtnPressOver2sec, ecallBtnStateType:{}", ecallBtnStateType));
    }
}

void CeCallMachine::setACallBtnStateTypeViaBtnPressOver2sec(Poco::Timer&) 
{
    if (acallBtnStateType == ButtonState_Type::SHORT_PRESSED) {
		acallBtnStateType = ButtonState_Type::LONG_PRESSED;
		_logger.notice("%s", fmt::format("setACallBtnStateTypeViaBtnPressOver2sec, acallBtnStateType:{}", acallBtnStateType));
    }
}
// >> [LE0WNC-2616]

//<< [LE0WNC-9450] TracyTu, adjust redial strategy 
bool CeCallMachine::isRedialBeyondMaxAttempts() 
{
	_logger.imp("%s", fmt::format("[AT] isRedialBeyondMaxAttempts dial_attempts:{} ", dial_attempts)); // << [LE022-5019] 20240925 EasonCCLiao >>
	bool result = false;
	if((dial_attempts >= getDialAttempsEU()))
	{
		_logger.notice("isRedialBeyondMaxAttempts");
		result = true;
	}
	return result;
}

void CeCallMachine::setFirstDialTime()
{
	if(dial_attempts==1U)
	{
		const LocalDateTime now;
		m_first_dial_time = now; 
		string fistTime(DateTimeFormatter::format(m_first_dial_time, "%Y_%m_%d_%H_%M_%S_%i"));
		_logger.information("%s", fmt::format("setFirstDialTime m_first_dial_time:{} ", fistTime.c_str()));
	}
}

void CeCallMachine::incRedialAttempts()
{
	dial_attempts ++;
}

bool CeCallMachine::isLastDialAttempts()const
{
	return (dial_attempts >= getDialAttempsEU());
}

bool CeCallMachine::tryRedial()
{
	bool result = false;
	if(!isRedialBeyondMaxAttempts()) 
	{		
        pecallMgr->peCallTrace()->setBBSSingleTag(BBS_ECALL_REDIAL_ATTEMPT, static_cast<uint8_t>(dial_attempts)); //<< [LE0-10138] TracyTu, eCall trace develop >>
		m_redialtimer.stop();
		m_redialtimer.setStartInterval(static_cast<long>(getRedialTimer())*1000);
		m_redialtimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_redial_timeout));
		result = true;
	}
	return result;
}

bool CeCallMachine::isECallActive()
{
	bool result = false;
	if((eCallSubState::ECALL_SUB_ACTIVE == getEcallCallSubState()) || (eCallSubState::ECALL_SUB_INCOMING == getEcallCallSubState()) || 
			 (eCallSubState::ECALL_SUB_DIAL == getEcallCallSubState()) || (eCallSubState::ECALL_SUB_REDIAL == getEcallCallSubState()))
	{
		_logger.notice("%s", fmt::format("ECall is active and call sub state is :{}", getEcallCallSubState()));
		result = true;
	}
	else
	{
		_logger.information("%s", fmt::format("ECall is not active and call sub state is :{}", getEcallCallSubState()));	
		result = false;
	}
	
	return result;
}

void CeCallMachine::resetRedialAttempts(eCallState currCallState, bool isNewCall)
{
	_logger.imp("%s", fmt::format("[AT] ResetRedialAttempts currCallState:{}, isNewCall:{}, dial_attempts:{} ", currCallState, isNewCall, dial_attempts)); // << [LE022-5019] 20240925 EasonCCLiao >>
	if(isNewCall || (currCallState > eCall_state_for_redial)) 
	{
		_logger.notice("%s", fmt::format("dial_attempts is reset from eCall_state_for_redial:{} to currCallState:{}, isNewCall:{}", eCall_state_for_redial, currCallState, isNewCall));
		dial_attempts = 1U;
		eCall_state_for_redial = currCallState;
	}

	if(isNewCall)
	{
		setFirstDialTime();  
	}
}

uint32_t CeCallMachine::getRedialTimer()
{
	uint32_t redial_timer = 0U;

	if((eCall_state_for_redial == eCallState::ECALL_CALL) && (getDialAttempsEU() > 0U))
	{
		LocalDateTime now;
		int64_t diffTime = static_cast<int64_t>(now.timestamp().epochTime()) - m_first_dial_time.timestamp().epochTime(); 
		string nowTime(DateTimeFormatter::format(now, "%Y_%m_%d_%H_%M_%S_%i"));
		string fistTime(DateTimeFormatter::format(m_first_dial_time, "%Y_%m_%d_%H_%M_%S_%i"));
		_logger.notice("%s", fmt::format("getRedialTimer now:{}, m_redial_first_time:{}, diffTime:{}", nowTime.c_str(), fistTime.c_str(), diffTime));
        if(diffTime < (static_cast<int64_t>(getDialDurationTime()) * 60))
		{
			const uint32_t redial_interval = static_cast<uint32_t>(std::round((getDialDurationTime() * 60U) / 
										(getDialAttempsEU() != 0U ? getDialAttempsEU() : 1U))); //<< [LE0WNC-9450] TracyTu, adjust redial strategy 
			const uint32_t nextRedialTime  = redial_interval * dial_attempts;
			redial_timer =  (nextRedialTime > static_cast<uint32_t>(diffTime)) ? (nextRedialTime - static_cast<uint32_t>(diffTime)) : 0U;
		}
	}
     
	redial_timer = std::max(redial_timer, redial_delay_time);	

	_logger.notice("%s", fmt::format("getRedialTimer redial_timer:{}", redial_timer));

	return redial_timer;
}
//>> [LE0WNC-9450] 

//<< [REQ-0481764] TracyTu, the timer CALL_AUTO_ANSWER_TIME is not rearmed if the PSAP calls backs
void CeCallMachine::setEnterWCBTime()
{
	const LocalDateTime now;
	m_enter_WCB_time = now;
	//<< [REQ-0481764] TracyTu, recover call back time when macchina crash
	m_enter_WCB_epoch_time = static_cast<uint64_t>(m_enter_WCB_time.timestamp().epochTime());  
    pecallMgr->peCallXmlConfig()->saveEcallEnterWCBTime(m_enter_WCB_epoch_time);
	//>> [REQ-0481764] 
	string enterWCBTime(DateTimeFormatter::format(m_enter_WCB_time, "%Y_%m_%d_%H_%M_%S_%i"));
	_logger.notice("%s", fmt::format("set enter WCB time:{} ", enterWCBTime.c_str()));	
}

void CeCallMachine::setExitWCBTime()
{
	const LocalDateTime now;
	m_exit_WCB_time = now;
	string exitWCBTime(DateTimeFormatter::format(m_exit_WCB_time, "%Y_%m_%d_%H_%M_%S_%i"));
	_logger.notice("%s", fmt::format("set exit WCB time:{} ", exitWCBTime.c_str()));	
	calElapsedTimeInWCB();
	calRemainingTimeInWCB(getElapsedTimeInWCB());
}

void CeCallMachine::calElapsedTimeInWCB()
{
	//<< [LE0-11349] TracyTu, Can't back to "CALL OFF" state after cold boot
	uint64_t elapsed_time = 0U;

	if(static_cast<uint64_t>(m_exit_WCB_time.timestamp().epochTime()) > m_enter_WCB_epoch_time)
	{
	   elapsed_time = static_cast<uint64_t>(m_exit_WCB_time.timestamp().epochTime()) - m_enter_WCB_epoch_time;		
	}
	//>> [LE0-11349] 
	m_elapsed_time_in_WCB = static_cast<uint32_t>(elapsed_time);  //<< [REQ-0481764] TracyTu, recover call back time when macchina crash >>
	_logger.notice("%s", fmt::format("calElapsedTimeInWCB elapsedTime:{}", m_elapsed_time_in_WCB));
}

uint32_t CeCallMachine::getElapsedTimeInWCB()const
{
	return m_elapsed_time_in_WCB;
}

void CeCallMachine::calRemainingTimeInWCB(uint32_t elapsedTime)
{
	m_remaining_time_in_WCB = (m_remaining_time_in_WCB > elapsedTime) ? (m_remaining_time_in_WCB - elapsedTime) : 0U;
	
	//<< [REQ-0481764] TracyTu, recover call back time when macchina crash
	if(m_remaining_time_in_WCB > (getAutoAnswerTime() * 60U))
	{
	   m_remaining_time_in_WCB = getAutoAnswerTime() * 60U;
	}

	pecallMgr->peCallXmlConfig()->saveEcallRemainingTimeInWCB(m_remaining_time_in_WCB);
	//>> [REQ-0481764]
	
    _logger.notice("%s", fmt::format("calcRemainingTimeInWCB elapsedTime:{}, remainingTime:{}", elapsedTime, m_remaining_time_in_WCB));
}

void CeCallMachine::setRemainingTimeInWCB(uint32_t remainingTime)
{
	_logger.notice("%s", fmt::format("setRemainingTimeInWCB remainingTime:{}", remainingTime));
	m_remaining_time_in_WCB = remainingTime;
	pecallMgr->peCallXmlConfig()->saveEcallRemainingTimeInWCB(m_remaining_time_in_WCB); //<< [REQ-0481764] TracyTu, recover call back time when macchina crash >>
}

uint32_t CeCallMachine::getRemainingTimeInWCB()
{
	_logger.notice("%s", fmt::format("getRemainingTimeInWCB remainingTime:{}", m_remaining_time_in_WCB));
	return m_remaining_time_in_WCB; // << [LE0-8731] TracyTu >>
}
//>> [REQ-0481764]

void CeCallMachine::enterECallStateECallCallOff() {
	m_is_prompt1_played = false;  //<< [LE0-6399] TracyTu, Play beep after pressing the button for two seconds >>
    m_prompttimer.stop();
    pecallMgr->peCallAudio()->cleanNeedPlay();  // << [LE0-6062] 20230630 CK >>
    m_statetimer.stop();
    m_getSignalStrengthTimer.stop(); // << [LE022-5418] 20241023 EasonCCLiao >>

	setXcallStatus(XcallStatus::NO_XCALL); //<< [V3-REQ] TracyTu, for xCall status >>
    pecallMgr->peCallRemoteServer()->notifyRemoteCommand(XCALL_STATE_ECALL_OFF, std::string(""));
    pecallMgr->peCallXmlConfig()->saveEcallState(XCALL_STATE_ECALL_OFF);
    pecallMgr->peCallEventLog()->notifyUpdateEvent(ECALL_SESSION_STATE, static_cast<uint32_t>(ECALL_SESSION_STATE_STOPPED_OK));
    pecallMgr->peCallHandler()->putStringToStateQ(ECALL_LED_GREEN_SWITCHED_OFF, std::string(""));

    //<< [REQ-0481764] TracyTu, the timer CALL_AUTO_ANSWER_TIME is not rearmed if the PSAP calls backs
    m_is_already_in_WCB = false;
    //>> [REQ-0481764]

    setEcallMachineState(eCallState::ECALL_OFF);
	// << [LE0-8731] TracyTu
	setEcallMachineSubState(eCallState::ECALL_WCB_ECALL_IDLE);
	setEcallCallSubState(eCallSubState::ECALL_SUB_NONE);
	// >> [LE0-8731]
	pecallMgr->peCallTrace()->setBBSNoTag(); //<< [LE0-10138] TracyTu, eCall trace develop >>
}
void CeCallMachine::enterECallStateECallManualTrigger() {
    (void)pecallMgr->peCallAudio()->play_audio_with_language("005");

    m_statetimer.stop();
    m_statetimer.setStartInterval((5 + static_cast<long>(getCancelationTime())) * 1000);
    m_statetimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_manual_cancel_timeout), "ecall_manual_cancel_timer");
    pecallMgr->peCallXmlConfig()->saveEcallState(XCALL_STATE_ECALL_TRIGGERED);

    setEcallMachineState(eCallState::ECALL_MANUAL_TRIGGERED);
	
	pecallMgr->peCallTrace()->setBBSSingleTag(BBS_ECALL_CANCELLATION_TIME, static_cast<uint8_t>(getCancelationTime())); //<< [LE0-10138] TracyTu, eCall trace develop >>
}

void CeCallMachine::enterECallStateECallRegistration() {
    pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_CONFIRMED());

    m_prompttimer.stop();
	m_prompttimer.setStartInterval(0); //<< [LE0-6399] TracyTu, Play beep after pressing the button for two seconds >>
    m_prompttimer.setPeriodicInterval(2 * 60 * 1000);  // 2min
    m_prompttimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_registration_prompt_timeout), "ecall_registration_prompt_timer");

    pecallMgr->peCallFunction()->ecall_cellularnetwork_GetVoiceRegistration();

    m_statetimer.stop();
    m_statetimer.setStartInterval(static_cast<long>(ecall_get_registration_time()) * 60 * 1000);
    m_statetimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_registration_timeout), "ecall_registration_timer");
    pecallMgr->peCallXmlConfig()->saveEcallState(XCALL_STATE_ECALL_REGISTRATION);

    setEcallMachineState(eCallState::ECALL_REGISTRATION);
	
	//<< [LE0-10138] TracyTu, eCall trace develop
	uint8_t registrationTime{};
	if(getPowerSource() == BBS_VEHICLE)
	{
		registrationTime = getWaitingNetworkVB();
	}
	else if(getPowerSource() == BBS_BACKUP)
	{
		registrationTime = getWaitingNetworkBub();
	}
	else
	{
		registrationTime = 0;
	}	
	pecallMgr->peCallTrace()->setBBSSingleTag(BBS_ECALL_REGISTRATION_TIME, registrationTime);
	//>> [LE0-10138]
}
void CeCallMachine::enterECallStateECallCall() {
    setEcallMachineState(eCallState::ECALL_CALL);
    _logger.notice("%s", fmt::format("ECALL_CALL call_sub_state:{} ", getEcallCallSubState()));
    uint32_t redial_interval = static_cast<uint32_t>(std::round((getDialDurationTime() * 60U) / (getDialAttempsEU() != 0U ? getDialAttempsEU() : 1U)));  //<< [LE0WNC-9450] TracyTu, adjust redial strategy
    m_is_already_in_WCB = false;                                                                                                                         //<< [LE0-6152]] TracyTu, Recalculate WCB time when trigger new call in WCB >>

    if (eCallSubState::ECALL_SUB_NONE == getEcallCallSubState()) {
		setEcallCallSubState(eCallSubState::ECALL_SUB_DIAL);

		pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(getEcallCallSubState()));
		pecallMgr->pCmdThread()->sendstringcmd(static_cast<uint32_t>(AUDIOMANAGER_TYPE), static_cast<uint32_t>(AUDIO_GPIO_UNMUTE), static_cast<uint16_t>(NORMAL), std::string(""));
		pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_DIALING());
		pecallMgr->peCallAudio()->set_audio_device(ecall_get_audio_device());
		pecallMgr->peCallAudio()->playAudioWithLanguage003();  // << [LE0-6062] 20230630 CK >>

		ecall_make_fast_ecall();

		//<< [REQ-0481752] TracyTu, adjust the play position of prompt 4
		m_prompttimer.stop();
		m_prompttimer.setStartInterval(0); //<< [LE0-6399] TracyTu, Play beep after pressing the button for two seconds >>
		m_prompttimer.setPeriodicInterval(5 * 1000);  // 5s
		m_prompttimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_ringtone_prompt_timeout), "ecall_ringtone_prompt_timer");
		pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_RINGTONE());
		//>> [REQ-0481752]

		//<< [LE0WNC-9450] TracyTu, adjust redial strategy
		resetRedialAttempts(eCallState::ECALL_CALL, true);
		m_statetimer.stop();
		m_statetimer.setStartInterval(static_cast<long>(std::max(redial_interval - redial_delay_time, 60U)) * 1000);
		m_statetimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_call_timeout), "ecall_call_timer");
		//>> [LE0WNC-9450]
    } else if (eCallSubState::ECALL_SUB_REDIAL == getEcallCallSubState()) {
		setEcallCallSubState(eCallSubState::ECALL_SUB_DIAL);
		pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(getEcallCallSubState()));
		pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_DIALING());
		pecallMgr->peCallAudio()->playAudioWithLanguage003();  // << [LE0-6062] 20230630 CK >>
		ecall_make_fast_ecall();
		//<< [REQ-0481752] TracyTu, adjust the play position of prompt 4
		m_prompttimer.stop();
		m_prompttimer.setStartInterval(0); //<< [LE0-6399] TracyTu, Play beep after pressing the button for two seconds >>
		m_prompttimer.setPeriodicInterval(5 * 1000);  // 5s
		m_prompttimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_ringtone_prompt_timeout), "ecall_ringtone_prompt_timer");
		pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_RINGTONE());
		//>> [REQ-0481752]

		//<< [LE0WNC-9450] TracyTu, adjust redial strategy
		incRedialAttempts();

		if (!isLastDialAttempts()) 
		{
			redial_interval = (redial_interval > redial_delay_time) ? (redial_interval - redial_delay_time) : redial_interval;
		}

		m_statetimer.stop();
		m_statetimer.setStartInterval(static_cast<long>(std::max(redial_interval, 60U)) * 1000);
		m_statetimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_call_timeout), "ecall_call_timer");
		//>> [LE0WNC-9450]
    } else if (eCallSubState::ECALL_SUB_DIAL == getEcallCallSubState()) {
		setEcallCallSubState(eCallSubState::ECALL_SUB_NONE);
		pecallMgr->peCallFunction()->ecall_HangUp();
    } else {
        _logger.warning("Get a call sub state NO processing!");
    }
    pecallMgr->peCallXmlConfig()->saveEcallState(XCALL_STATE_ECALL_CALL);
}
void CeCallMachine::enterECallStateECallMSDTransfer() {
    //<< [LE0WNC-9450] TracyTu, adjust redial strategy
    m_prompttimer.stop();
    pecallMgr->peCallAudio()->cleanNeedPlay();  // << [LE0-6062] 20230630 CK >>
    resetRedialAttempts(eCallState::ECALL_MSD_TRANSFER);
    //>> [LE0WNC-9450]
    if (eCallMsdState::ECALL_MSD_NONE == m_msd_state) {
		setMsdTimeoutType(BBS_MSD_SEND_TIMEOUT_T5); //<< [LE0-10138] TracyTu, eCall trace develop (phase 5) >>
		m_statetimer.stop();
		m_statetimer.setStartInterval(static_cast<long>(getT5()) * 1000); /* <<[LE0-7984] yusanhsu, add 500ms for timing issue*/  // << [LE0-15382] TracyTu, Add msd state when psap request
		m_statetimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_msd_transfer_timeout), "ecall_msd_transfer_timer");
	/* <<[LE0-7984] yusanhsu*/
	} else if (eCallMsdState::ECALL_MSD_SENDING_START == m_msd_state){
		// << [LE0-15382] TracyTu, Add msd state when psap request 
		m_statetimer.stop();
    } else if (eCallMsdState::ECALL_MSD_SENDING_MSD == m_msd_state) {
		setMsdTimeoutType(BBS_MSD_SEND_TIMEOUT_TRANSMISSION); //<< [LE0-10138] TracyTu, eCall trace develop (phase 5) >>
		m_statetimer.stop(); // << [LE0-15928] TracyTu, 20240826 >>
		m_statetimer.setStartInterval(static_cast<long>(getMsdTransmissionTime()) * 1000); /* <<[LE0-7984] yusanhsu, add 500ms for timing issue*/
		m_statetimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_msd_transfer_timeout), "ecall_msd_transfer_timer");
		// >> [LE0-15382] 
	/* >>[LE0-7984] yusanhsu*/
    } else if (eCallMsdState::ECALL_MSD_SENDING_LLACK_RECEIVED == m_msd_state) {
		setMsdTimeoutType(BBS_MSD_SEND_TIMEOUT_T6); //<< [LE0-10138] TracyTu, eCall trace develop (phase 5) >>
		m_statetimer.stop();
		m_statetimer.setStartInterval(static_cast<long>(getT6()) * 1000); /* <<[LE0-7984] yusanhsu, add 500ms for timing issue*/ // << [LE0-15382] TracyTu, Add msd state when psap request
		m_statetimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_msd_transfer_timeout), "ecall_msd_transfer_timer");
    } else {
        _logger.warning("Get a MSD state NO processing!");
    }

	pecallMgr->peCallXmlConfig()->saveEcallState(XCALL_STATE_ECALL_MSD_TRANSFER);
    setEcallMachineState(eCallState::ECALL_MSD_TRANSFER);
}
void CeCallMachine::enterECallStateECallVoiceCommunication() {
    //<< [REQ-0481764] TracyTu, the timer CALL_AUTO_ANSWER_TIME is not rearmed if the PSAP calls backs
    if (getEcallMachineState() == eCallState::ECALL_WAITING_FOR_CALLBACK)
	{
		setExitWCBTime();
	} 
    //>> [REQ-0481764]

    resetRedialAttempts(eCallState::ECALL_VOICE_COMMUNICATION);  //<< [LE0WNC-9450] TracyTu, adjust redial strategy >>
    pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTETEXT_IN_COMMUNICATION());

    pecallMgr->peCallHandler()->putStringToStateQ(ECALL_LED_GREEN_FIXED_LIGHT, std::string(""));

    pecallMgr->peCallRemoteServer()->notifyRemoteCommand(XCALL_STATE_ECALL_VOICE_COMMUNICATION, std::string(""));

    pecallMgr->peCallFunction()->ecall_reset_ivs();

    m_prompttimer.stop();
    pecallMgr->peCallAudio()->cleanNeedPlay();  // << [LE0-6062] 20230630 CK >>
    m_statetimer.stop();
    m_statetimer.setStartInterval(static_cast<long>(getCCFT()) * 60 * 1000);
    m_statetimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_clear_down_timeout), "ecall_clear_down_timer");

    pecallMgr->peCallXmlConfig()->saveEcallState(XCALL_STATE_ECALL_VOICE_COMMUNICATION);

    setEcallMachineState(eCallState::ECALL_VOICE_COMMUNICATION);

    pecallMgr->peCallTrace()->setBBSVoiceCommunication(); // << [LE0-10138] 20231225 CK >>
}
void CeCallMachine::enterECallStateECallWCB() {
	m_is_prompt1_played = false;  //<< [LE0-6399] TracyTu, Play beep after pressing the button for two seconds >>
    m_redialtimer.stop();
    m_statetimer.stop();
	m_msdRequestTimer.stop(); // << [LE0-15382] TracyTu, Add msd state when psap request >>

    //<< [REQ-0481764] TracyTu, the timer CALL_AUTO_ANSWER_TIME is not rearmed if the PSAP calls backs
    setEnterWCBTime();
    if (!m_is_already_in_WCB) {
        setRemainingTimeInWCB(getAutoAnswerTime() * 60U);
    }
	// << [LE0-8731] TracyTu
    if (getRemainingTimeInWCB() <= 0) {
        _logger.warning("%s", fmt::format("enterECallStateECallWCB but no remaining time"));
	 	ecall_trans_state(eCallState::ECALL_OFF);
    // >> [LE0-8731]
	} else {
		m_is_already_in_WCB = true;
		pecallMgr->peCallRemoteServer()->notifyRemoteCommand(XCALL_STATE_ECALL_WAITING_FOR_CALLBACK, std::to_string(getRemainingTimeInWCB()));
		//>> [REQ-0481764]

		call_back_time = 0;
		pecallMgr->peCallAudio()->cleanNeedPlay();  // << [LE0-6062] 20230630 CK >>
		m_prompttimer.stop();
		m_prompttimer.setStartInterval(0); //<< [LE0-6399] TracyTu, Play beep after pressing the button for two seconds >>
		m_prompttimer.setPeriodicInterval(pecallMgr->peCallRemoteServer()->getECALL_WAITING_FOR_CALLBACK_PROMPT_TIME() * 1000);
		m_prompttimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_callback_prompt_timeout), "ecall_callback_prompt_timer");

		pecallMgr->peCallXmlConfig()->saveEcallState(XCALL_STATE_ECALL_WAITING_FOR_CALLBACK);
		pecallMgr->peCallEventLog()->notifyUpdateEvent(ECALL_SESSION_STATE, static_cast<uint32_t>(ECALL_SESSION_STATE_WAIT));

		if (!pecallMgr->peCallAudio()->isCurrentOnPlay())
		{
			pecallMgr->pCmdThread()->sendstringcmd(static_cast<uint32_t>(AUDIOMANAGER_TYPE), static_cast<uint32_t>(AUDIO_GPIO_MUTE), static_cast<uint16_t>(NORMAL), std::string(""));
		}

		pecallMgr->peCallHandler()->putStringToStateQ(ECALL_LED_GREEN_SWITCHED_OFF, std::string(""));

		setEcallMachineState(eCallState::ECALL_WAITING_FOR_CALLBACK);
		setEcallMachineSubState(eCallState::ECALL_WCB_ECALL_IDLE);
	    pecallMgr->peCallTrace()->setBBSSingleTag(BBS_ECALL_AUTO_ANSWER_TIME, static_cast<uint8_t>(getAutoAnswerTime())); //<< [LE0-10138] TracyTu, eCall trace develop >>
	}
}
void CeCallMachine::enterECallStateECallWCBIdle() {
    m_statetimer.stop();

    //<< [LE0-6152]] TracyTu, Recalculate WCB time when trigger new call in WCB
    setEnterWCBTime();
    pecallMgr->peCallRemoteServer()->notifyRemoteCommand(XCALL_STATE_ECALL_WAITING_FOR_CALLBACK, std::to_string(getRemainingTimeInWCB()));
    //>> [LE0-6152]]
    call_back_time = 0;
    pecallMgr->peCallAudio()->cleanNeedPlay();  // << [LE0-6062] 20230630 CK >>

	m_prompttimer.stop();
	m_prompttimer.setStartInterval(0); //<< [LE0-6399] TracyTu, Play beep after pressing the button for two seconds >>
    m_prompttimer.setPeriodicInterval(pecallMgr->peCallRemoteServer()->getECALL_WAITING_FOR_CALLBACK_PROMPT_TIME() * 1000);
    m_prompttimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_callback_prompt_timeout), "ecall_callback_prompt_timer");

    pecallMgr->peCallXmlConfig()->saveEcallState(XCALL_STATE_ECALL_WAITING_FOR_CALLBACK);
    pecallMgr->peCallHandler()->putStringToStateQ(ECALL_LED_GREEN_SWITCHED_OFF, std::string(""));

    setEcallMachineSubState(eCallState::ECALL_WCB_ECALL_IDLE);
}
void CeCallMachine::enterECallStateECallWCBManualTrigger() {
    (void)pecallMgr->peCallAudio()->play_audio_with_language("005");

    m_prompttimer.stop();
    m_statetimer.stop();
    m_statetimer.setStartInterval((5 + static_cast<long>(getCancelationTime())) * 1000);
    m_statetimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_manual_cancel_timeout), "ecall_manual_cancel_timer");

    pecallMgr->peCallXmlConfig()->saveEcallState(XCALL_STATE_ECALL_TRIGGERED);

    setEcallMachineSubState(eCallState::ECALL_WCB_ECALL_MANUAL_TRIGGERED);
	pecallMgr->peCallTrace()->setBBSSingleTag(BBS_ECALL_CANCELLATION_TIME, static_cast<uint8_t>(getCancelationTime())); //<< [LE0-10138] TracyTu, eCall trace develop >>

}

//<< [LE0-6399] TracyTu, Play beep after pressing the button for two seconds
void CeCallMachine::processLongPressInEcallWCB()
{
	ecall_triggered_information_record(false); // << [LE0WNC-2410] 20230214 CK >>
	setExitWCBTime(); //<< [LE0-6152]] TracyTu, Recalculate WCB time when trigger new call in WCB >>
	if(false == isEcallManualCanCancel())
	{
		setEcallCallSubState(eCallSubState::ECALL_SUB_NONE);
		pecallMgr->peCallAudio()->playAudioWithLanguage008();  //[LE0-6465] TracyTu, Play prompt 8 when do auto trigger
		ecall_trans_state(eCallState::ECALL_CALL);
	}
	else
	{
		ecall_trans_state(eCallState::ECALL_WCB_ECALL_MANUAL_TRIGGERED);
	}
}
//>> [LE0-6399] 	

void CeCallMachine::processIncomingCallInEcallWCB(CeCallFunction::sCallState callState)
{
	_logger.notice("processIncomingCallInEcallWCB");
	uint8_t call_state = ecall_get_call_state(callState.getCallId(), callState.getCallState());
	if(0x02U == callState.getCallDirection())
	{   
		_logger.notice( "%s", fmt::format("call_state:{} call_sub_state:{}", call_state, getEcallCallSubState()));
		if(static_cast<uint8_t>(FIH_ECALL_VOICE_STATE_INCOMING) == call_state )
		{
			m_beepTimer.stop(); //<<[LE0-6399][LE0-8913]yusanhsu>>
			pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_INCOMINF_CALL());
			(void)pecallMgr->peCallAudio()->play_audio_with_language("012"); // << [LE0-7069][LE0-5724][LE0-5671] 20230621 CK >>
			setEcallCallSubState(eCallSubState::ECALL_SUB_INCOMING);
			pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(getEcallCallSubState())); 
			pecallMgr->peCallFunction()->ecall_Answer(static_cast<uint8_t>(m_callId)); //[LE0WNC-2846] TracyTu, answer incoming calls controlled by ecall
		}
		else if(static_cast<uint8_t>(FIH_ECALL_VOICE_STATE_CONVERSATION) == call_state)
		{
			if(eCallSubState::ECALL_SUB_INCOMING == getEcallCallSubState())
			{
				pecallMgr->peCallTrace()->setBBSSingleTag(BBS_ECALL_VOICE_STATE, BBS_VOICE_INCOMING); //<< [LE0-10138] TracyTu, eCall trace develop >>
				pecallMgr->pCmdThread()->sendstringcmd(static_cast<uint32_t>(AUDIOMANAGER_TYPE), static_cast<uint32_t>(AUDIO_GPIO_UNMUTE), static_cast<uint16_t>(NORMAL), std::string(""));	
				_logger.imp("%s", fmt::format("[AT] start trans_state to ECALL_VOICE_COMMUNICATION: in call, call_sub_state:{}", getEcallCallSubState())); // << [LE022-5019][LE0WNC-9171] 20240925 EasonCCLiao >>
				setEcallCallSubState(eCallSubState::ECALL_SUB_ACTIVE);
				pecallMgr->peCallEventLog()->notifyUpdateEvent(PSAP_CALL_STATE, convert_u32(getEcallCallSubState())); 
				pecallMgr->peCallHandler()->putStringToStateQ(ECALL_LED_GREEN_FIXED_LIGHT, std::string(""));
				ecall_trans_state(eCallState::ECALL_VOICE_COMMUNICATION);
			}
		}
		else if(static_cast<uint8_t>(FIH_ECALL_VOICE_STATE_END) == call_state )
		{
			// << [LE0-8731] TracyTu
			if (eCallSubState::ECALL_SUB_INCOMING == getEcallCallSubState()) {
				pecallMgr->peCallTrace()->setBBSHangupTag(callState.getDiscCause()); //<< [LE0-10138] TracyTu, eCall trace develop >>	
				setExitWCBTime();
				setEnterWCBTime();
				_logger.notice( "%s",fmt::format("ECALL_FLOW_URC_CALL_STATUS_IND, m_remaining_time_in_WCB_test:{}", m_remaining_time_in_WCB));

				pecallMgr->peCallRemoteServer()->notifyPromptText(pecallMgr->peCallRemoteServer()->getECALL_FEEDBACK_PROMPTTEXT_FINISHED());

				(void)pecallMgr->peCallAudio()->play_audio_with_language("011");
				setEcallCallSubState(eCallSubState::ECALL_SUB_NONE);

				if (m_remaining_time_in_WCB <= 0) {
					ecall_trans_state(eCallState::ECALL_OFF);
				}
			}
			// >> [LE0-8731]
		}
		else
		{
			_logger.warning("Get a call state NO processing!");
		}
	}

}

// << [LE0-13731] 20240514 CK
void CeCallMachine::updateHpMicroTestState(const bool duringTest) {
    _logger.notice("%s", fmt::format("updateHpMicroTestState duringTest:{}", duringTest));
    if (duringTest) {
        pecallMgr->pCmdThread()->sendstringcmd(static_cast<uint32_t>(MCUMANAGER_TYPE), static_cast<uint32_t>(MCU_MUTE_IVI), static_cast<uint16_t>(NORMAL), std::string("on"));
    } else {
        m_latestCallStatus3A9 = CallStatus3A9::CALL_OFF;
        pecallMgr->pCmdThread()->sendstringcmd(static_cast<uint32_t>(MCUMANAGER_TYPE), static_cast<uint32_t>(MCU_MUTE_IVI), static_cast<uint16_t>(NORMAL), std::string("off"));
        update3A9StateV2();
    }
}
CallStatus3A9 CeCallMachine::export3A9StateV2() {
    CallStatus3A9 callStatus3A9 = CallStatus3A9::CALL_OFF;
    switch (ecall_current_state) {
        case XCALL_STATE_ECALL_TRIGGERED:
        case XCALL_STATE_ECALL_REGISTRATION:
        case XCALL_STATE_ECALL_CALL:
        case XCALL_STATE_ECALL_MSD_TRANSFER:
            callStatus3A9 = CallStatus3A9::ECALL_DIAL;
            break;
        case XCALL_STATE_ECALL_VOICE_COMMUNICATION:
            callStatus3A9 = CallStatus3A9::ECALL_VOICE_COMMUNICATION;
            break;
        case XCALL_STATE_ECALL_WAITING_FOR_CALLBACK:
            callStatus3A9 = CallStatus3A9::ECALL_WCB;
            break;
        default:
            switch (bcall_current_state) {
                case XCALL_STATE_BCALL_TRIGGERED:
                case XCALL_STATE_BCALL_REGISTRATION:
                case XCALL_STATE_BCALL_CALL:
                    callStatus3A9 = CallStatus3A9::ACALL_DIAL;
                    break;
                case XCALL_STATE_BCALL_VOICE_COMMUNICATION:
                    callStatus3A9 = CallStatus3A9::ACALL_VOICE_COMMUNICATION;
                    break;
                case XCALL_STATE_BCALL_WAITING_FOR_CALLBACK:
                    callStatus3A9 = CallStatus3A9::ACALL_WCB;
                    break;
                default:
                    callStatus3A9 = CallStatus3A9::CALL_OFF;
                    break;
            }
            break;
    }

    return callStatus3A9;
}
void CeCallMachine::update3A9StateV2() {
    _logger.notice("%s", fmt::format("update3A9StateV2 ecall_current_state:{} bcall_current_state:{}", ecall_current_state, bcall_current_state));

    const CallStatus3A9 callStatus3A9 = export3A9StateV2();
    const uint32_t callStatus3A9Uint32 = static_cast<uint32_t>(callStatus3A9);
    const uint32_t latestCallStatus3A9Uint32 = static_cast<uint32_t>(m_latestCallStatus3A9);
    if (callStatus3A9Uint32 != latestCallStatus3A9Uint32) {
        _logger.notice("%s", fmt::format("update3A9StateV2 latestCallStatus3A9:0x{:x}, callStatus3A9:0x{:x}", latestCallStatus3A9Uint32, callStatus3A9Uint32));
        const bool islatestCallStatus3A9Unknown = (latestCallStatus3A9Uint32 == static_cast<uint32_t>(CallStatus3A9::UNKNOWN));
        const bool isECallMsdOn = ((callStatus3A9Uint32 & 0x10) != 0);              // (bit control) ECall ongoing (include WCB)
        const bool isACallMsdOn = ((callStatus3A9Uint32 & 0x20) != 0);              // (bit control) ACall ongoing (include WCB)
        const bool isLatestECallMsdOn = ((latestCallStatus3A9Uint32 & 0x10) != 0);  // (bit control) ECall ongoing (include WCB)
        const bool isLatestACallMsdOn = ((latestCallStatus3A9Uint32 & 0x20) != 0);  // (bit control) ACall ongoing (include WCB)
        if (islatestCallStatus3A9Unknown || (isLatestECallMsdOn && !isECallMsdOn)) {
            pecallMgr->pCmdThread()->sendstringcmd(MCUMANAGER_TYPE, MCU_REPORT_ECALL_MDS_STATE, NORMAL, std::string("off"));
        } else if (islatestCallStatus3A9Unknown || (isLatestACallMsdOn && !isACallMsdOn)) {
            pecallMgr->pCmdThread()->sendstringcmd(MCUMANAGER_TYPE, MCU_REPORT_ACALL_MDS_STATE, NORMAL, std::string("off"));
        }

        const bool isMuteIvi = ((callStatus3A9Uint32 & 0xF) != 0);  // (bit control)
        const std::string cmdDataMuteIvi = isMuteIvi ? std::string("on") : std::string("off");
        pecallMgr->pCmdThread()->sendstringcmd(static_cast<uint32_t>(MCUMANAGER_TYPE), static_cast<uint32_t>(MCU_MUTE_IVI), static_cast<uint16_t>(NORMAL), cmdDataMuteIvi);

        if (isECallMsdOn) {
            pecallMgr->pCmdThread()->sendstringcmd(MCUMANAGER_TYPE, MCU_REPORT_ECALL_MDS_STATE, NORMAL, std::string("on"));
        } else if (isACallMsdOn) {
            pecallMgr->pCmdThread()->sendstringcmd(MCUMANAGER_TYPE, MCU_REPORT_ACALL_MDS_STATE, NORMAL, std::string("on"));
        }
        m_latestCallStatus3A9 = callStatus3A9;
    }
}
// >> [LE0-13731]
//<< [V3-REQ] TracyTu, for xCall status
void CeCallMachine::reportFault()
{   
	_logger.notice("setXcallStatus reportFault");
    m_Fault_triggered = true; 
	saveXcallStatusToMCU(XcallStatus::FAULT);
}

void CeCallMachine::clearFault()
{   
    m_Fault_triggered = false; 
	_logger.notice("%s", fmt::format("setXcallStatus clearFault status:{}", m_status));
	setXcallStatus(m_status); 
}

void CeCallMachine::setXcallStatus(XcallStatus status)
{
	_logger.notice("%s", fmt::format("setXcallStatus status:{}", status));
	m_status = status;
	if(canStore())
	{
		saveXcallStatusToMCU(m_status);
	}
}

void CeCallMachine::saveXcallStatusToMCU(XcallStatus status)
{
	_logger.notice("%s", fmt::format("saveXcallStatusToMCU status:{}", status));
	const int data = static_cast<int>(status);
	pecallMgr->pCmdThread()->sendstringcmd(static_cast<uint32_t>(MCUMANAGER_TYPE), static_cast<uint32_t>(MCU_REPORT_XCALL_STATUS), static_cast<uint16_t>(NORMAL), std::to_string(data));
    mIsNeedResyncXCallStatus = false; // << [LE0-9412] 20240109 CK >>
}
//>> [V3-REQ] 
  
// << [LE0-12080] TracyTu, Support one image 
bool CeCallMachine::isEcallTypePE112() const
{
    bool ret = false;
    if(getEcallType() == RTBM_ECALL_TYPE_PE112)
    {
        ret = true;
    }
    else
    {
        _logger.notice("%s", fmt::format("ecall_type:{}", getEcallType()));
    }
    return ret;
}
// >> [LE0-12080]

// << [LE0-13446] TracyTu, delete MSD data log file
void CeCallMachine::setEcallDataLogFileUpdateTime(uint64_t const time)
{
	_logger.notice("%s", fmt::format("setEcallDataLogFileUpdateTime FileTime:{}", time));
	m_data_logfile_update_time = time;
	pecallMgr->peCallXmlConfig()->saveEcallDataLogFileUpdateTime(m_data_logfile_update_time);
    checkEcallDataFileExpiry();
}

uint64_t CeCallMachine::getEcallDataLogFileUpdateTime() const
{
	_logger.notice("%s", fmt::format("getEcallDataLogFileUpdateTime FileTime:{}", m_data_logfile_update_time));
	return m_data_logfile_update_time; 
}

uint32_t CeCallMachine::getEcallDataFileElaspedTime()
{
	LocalDateTime now;
	string nowTime(DateTimeFormatter::format(now, "%Y_%m_%d_%H_%M_%S_%i"));

	uint64_t elapsedTime = 0U;
	if(static_cast<uint64_t>(now.timestamp().epochTime()) > m_data_logfile_update_time)
	{
		elapsedTime = static_cast<uint64_t>(now.timestamp().epochTime()) - m_data_logfile_update_time;
	}
	_logger.notice("%s", fmt::format("getEcallDataFileElaspedTime now:{}, m_data_logfile_update_time:{}, elapsedTime:{}", nowTime.c_str(), m_data_logfile_update_time, elapsedTime));
	return static_cast<uint32_t>(elapsedTime);
}

void CeCallMachine::checkEcallDataFileExpiry()
{
	uint32_t elapsedTime = getEcallDataFileElaspedTime();
   	_logger.notice("%s", fmt::format("checkEcallDataFileExpiry elapsedTime:{}", elapsedTime)); 
	if(INVALID_DATA_LOGFILE_TIME_SEC == m_data_logfile_update_time)
	{
		_logger.notice("checkEcallDataFileExpiry no valid file time stamp");
	} 
	else if(elapsedTime >= getDataLogFileExpiredSecond())
	{
		_logger.notice("checkEcallDataFileExpiry remove expired msd file");
		pecallMgr->peCallMSD()->remvoe_msd_log();
		setEcallDataLogFileUpdateTime(INVALID_DATA_LOGFILE_TIME_SEC); 
	}
	else
	{
		uint32_t remainingTime = getDataLogFileExpiredSecond() - elapsedTime;
		_logger.notice("%s", fmt::format("checkEcallDataFileExpiry remainingTime:{}", remainingTime)); 
		m_msdExpreTimer.stop();
		m_msdExpreTimer.setStartInterval(remainingTime * 1000); 
		m_msdExpreTimer.start(TimerCallback<CeCallMachine>(*this, &CeCallMachine::ecall_callback_msd_log_expired), "ecall_callback_msd_log_timer");
	}  
}

uint32_t CeCallMachine::getDataLogFileExpiredSecond() const { 
	return pecallMgr->peCallMachine()->getDataLogFileExpiredTime() * 60; //12*60 + 50 min /*(12*60+50)*60*/
}  
// >> [LE0-13446] 

// << [LE022-2691] 20240524 ZelosZSCao
bool CeCallMachine::isPhoneNumberNull() const {
    bool isNull = false;
    std::string ecall_number = getEcallNumber();
    if(true == m_diag_test_ecall_session) {
        ecall_number = getEcallTestNumber();
    }
    if (isEcallTypePE112() && (ecall_number.compare("112") == 0)) {
        ecall_number = "null";
    }
    if ((ecall_number.compare("null") == 0) || (ecall_number.length() == 0)) {
        isNull = true;
    }
    _logger.notice("%s", fmt::format("[net_reg_polling] isPhoneNumberNull:{}", isNull));
    return isNull;
}

bool CeCallMachine::isAnyCellFound() const {
    bool found = false;
    nad_network_cellinfo_list_t cell_info_list;

    NAD_RIL_Errno ret = nad_network_get_cell_info_list(&cell_info_list);
    if(ret == NAD_E_SUCCESS && (cell_info_list.num > 0)) {
        found = true;
    }
    _logger.notice("%s", fmt::format("[net_reg_polling] isAnyCellFound:{}", found));
    return found;
}

void CeCallMachine::pollingCellInfo(bool start) const {
    _logger.notice("%s", fmt::format("[net_reg_polling] pollingCellInfo:{}", start));
    pecallMgr->peCallHandler()->putAnyToStateQ(start ? ECALL_FLOW_REGISTRATION_POLLING_START: ECALL_FLOW_REGISTRATION_POLLING_SUCCESS, std::any());
}
// >> [LE022-2691]

//<< [LE0-14407] TracyTu, eCall recovery
void CeCallMachine::setEventRcvStatus(genericEvent event, bool rcvStatus) {
    _logger.notice("%s", fmt::format("setEventRcvStatus event:{}, rcvStatus:{}", event, rcvStatus));
    m_event_rcv_status.set(event, rcvStatus);
    notifyToCheckRecovery();
}

bool CeCallMachine::getEventRcvStatus(genericEvent event) {
	_logger.information("%s", fmt::format("getEventRcvStatus event:{}, rcv_status:{}", event, m_event_rcv_status.test(event))); 
	return m_event_rcv_status.test(event);
}

void CeCallMachine::notifyToCheckRecovery() {
    if (needEcallRecovery()) {
        pecallMgr->peCallHandler()->putAnyToStateQ(ECALL_FLOW_RECOVERY_REQ, std::string(""));
    }
}

void CeCallMachine::initStartUp() {
    std::string beforeShotdownParkMode = getInternalParkMode();
    uint32_t beforeShotdownLifeCycleState = getInternalLifeCycleState();
    std::string beforeShotdownThermalMitigation = getInternalThermalMitigation();
    uint32_t beforeShotdownECallState = getEcallState();
    _logger.notice("%s", fmt::format("initStartUp beforeShotdown state is ECALL_STATE:{} LIFE_CYCLE_STATE:{} LCM_THERMAL_MITIGATION:{}",
        beforeShotdownECallState, beforeShotdownLifeCycleState, beforeShotdownThermalMitigation));

    //>> [LE0-11860]
    if((XCALL_STATE_ECALL_OFF != beforeShotdownECallState) and (XCALL_STATE_ECALL_WAITING_FOR_CALLBACK != beforeShotdownECallState))
    {
        setNeedToRetriggerECallAfterReboot(true);
    }
    //<<[LE0WNC-2360] yusanhsu,implement REQ-0315153

    else if (XCALL_ECALL_BTN_PRESS == pecallMgr->peCallMachine()->getEcallButtonState()) {
        const bool isBeforeShotdownParkModeOFF = beforeShotdownParkMode.compare(std::string("0")) == 0; // [LE0-15429] 20240731 EasonCCLiao
        const bool isNeesRecoveryPressECallState = (beforeShotdownECallState == static_cast<uint32_t>(XCALL_STATE_ECALL_OFF))
            || (beforeShotdownECallState == XCALL_STATE_ECALL_WAITING_FOR_CALLBACK);
        const bool isNeesRecoveryPressLCMState = (beforeShotdownLifeCycleState != static_cast<uint32_t>(LCM_STATE_UPDATE))
            && (beforeShotdownLifeCycleState != static_cast<uint32_t>(LCM_STATE_INACTIVE))
            && (beforeShotdownLifeCycleState != static_cast<uint32_t>(LCM_STATE_SERVICE_WATCHER)) // << [LE0-16547] CongHuaPeng >>
            && (beforeShotdownLifeCycleState != static_cast<uint32_t>(LCM_STATE_BEFORE_SLEEP));

        if (pecallMgr->peCallMachine()->isMaunalEcallActive() && isBeforeShotdownParkModeOFF && isNeesRecoveryPressECallState && isNeesRecoveryPressLCMState) { // [LE0-15429] 20240731 EasonCCLiao
            setNeedToRetriggerECallAfterReboot(true);
            _logger.warning("%s", fmt::format("CeCallMachine::init REQ-0315153 recovery, need to trigger eCall{}", isNeedToRetriggerECallAfterReboot()));
        } else {
            _logger.notice("%s", fmt::format("CeCallMachine::init REQ-0315153 recovery, no need to trigger eCall, ECALL_ACTIVE:{} park_mode:{} ecall_state:{} lcm_state:{}",
                pecallMgr->peCallMachine()->isMaunalEcallActive(), beforeShotdownParkMode, beforeShotdownECallState, beforeShotdownLifeCycleState)); // << [LE0-16547] CongHuaPeng >>
        }
        // << [LE0-15429][LE0-12307] 20240731 EasonCCLiao
        pecallMgr->peCallMachine()->setEcallButtonState(XCALL_ECALL_BTN_RELEASE);
        pecallMgr->peCallXmlConfig()->saveEcallButtonState(XCALL_ECALL_BTN_RELEASE);
        // >> [LE0-15429][LE0-12307]
    } else {
        _logger.notice("%s", fmt::format("initStartUp no need to trigger eCall, ecall_button_state:{} ecall_state:{}",
            pecallMgr->peCallMachine()->getEcallButtonState(), beforeShotdownECallState)); // << [LE0-16547] CongHuaPeng >>
    }
    //<<[LE0WNC-2360]

    //<< [REQ-0481764] TracyTu, recover call back time when macchina crash
    if (XCALL_STATE_ECALL_OFF != beforeShotdownECallState) {
        _logger.notice("%s", fmt::format("checkStartupBehavior sending ECALL_FLOW_CALL_CRASH_RECOVERY_IND"));
        pecallMgr->peCallHandler()->putStringToStateQ(ECALL_FLOW_CALL_CRASH_RECOVERY_IND, std::string(""));
    }
    //>> [REQ-0481764]

    recoverLcmParkMode(beforeShotdownParkMode);
    recoverLcmState(beforeShotdownLifeCycleState);
    recoverLcmThermal(std::string("0")); // << [LE0-14407] 20240627 CK >> // Reset Thermal level to 0 (because cold boot not receive currently Thermal level)
    notifyToCheckRecovery(); // << [LE0-14407] 20240627 CK >>

    checkEcallDataFileExpiry();  // << [LE0-13446] TracyTu, delete MSD data log file >>
}

void CeCallMachine::recoverLcmParkMode(std::string park_mode) {
    if (getEventRcvStatus(PARK_MODE)) {
        // reset park mode
        resetInternalParkMode();
        _logger.notice("%s", fmt::format("recoverLCMParkMode park_mode:{}", park_mode)); // << [LE0-14407] 20240627 CK >>
        handleLcmParkMode(park_mode);
    }
}

void CeCallMachine::handleLcmParkMode(std::string park_mode){
	pecallMgr->peCallMachine()->setInternalParkMode(park_mode);
	pecallMgr->peCallXmlConfig()->saveParkMode(park_mode); // << [LE0-6378][LE0-6366] 20230718 CK >>
}

void CeCallMachine::recoverLcmState(uint32_t lcm_state) {
    if (getEventRcvStatus(LCM_STATE)) {
        // reset lcm status
        pecallMgr->peCallMachine()->resetInternalLifeCycleState();
        _logger.notice("%s", fmt::format("recoverLCMState lcm_state:{}", lcm_state)); // << [LE0-14407] 20240627 CK >>
        handleLcmState(lcm_state);
    }
}

void CeCallMachine::handleLcmState(uint32_t state)
{
	// << [LE0-4015] 20230526 CK
    uint32_t beforeState = pecallMgr->peCallMachine()->getInternalLifeCycleState();
    uint32_t afterState = state;
    _logger.notice("%s", fmt::format("LIFE_CYCLE_STATE: {} -> {}", beforeState, afterState));
    if (beforeState != afterState) {
        pecallMgr->peCallMachine()->setInternalLifeCycleState(afterState);
        pecallMgr->peCallXmlConfig()->saveLifeCycleState(afterState); // << [LE0-10901] 20240223 ck >>
        switch (beforeState) {
            case LCM_STATE_INACTIVE:
            case LCM_STATE_SERVICE_WATCHER:
            case LCM_STATE_NOMINAL:
            case LCM_STATE_EMERGENCY:
            case LCM_STATE_BEFORE_SLEEP:
            case LCM_STATE_LOW_POWER_CALLBACK: {
                break;
            }
            case LCM_STATE_UPDATE: {
                pecallMgr->peCallXmlConfig()->loadConfig(); // << [LE0-4466] 20230602 CK: REQ-0481704 >>
                pecallMgr->peCallHandler()->putStringToStateQ(ECALL_LED_RED_SWITCHED_OFF, std::string(""));
                break;
            }
            default: {
				if(ecall_internal_data::LCM_STATE_UNINITIALIZED_VALUE != beforeState)
				{
	                _logger.warning("%s", fmt::format("lcm LIFE_CYCLE_STATE:{} out of scope!", beforeState));				
				}
				else
				{
				    _logger.notice("%s", fmt::format("lcm LIFE_CYCLE_STATE:beforeState is uninitialized!"));					
				}
                break;
            }
        }
       
        switch (afterState) {
            case LCM_STATE_INACTIVE:
            case LCM_STATE_SERVICE_WATCHER:
            case LCM_STATE_BEFORE_SLEEP:
            case LCM_STATE_LOW_POWER_CALLBACK: {
                break;
            }
            case LCM_STATE_EMERGENCY: {
                pecallMgr->peFihSysApiInterface()->powerWakelockDisableECallBtn();
                break;
            }
            case LCM_STATE_NOMINAL: {
                pecallMgr->peCallEventLog()->notifyUpdateEvent(VEHICLE_CAN, static_cast<uint32_t>(VEHICLE_CANSTATE_NORMAL));

                //sync can message config and save file
                (void)pecallMgr->peCallAudio()->get_ivs_language(); // << [LE0-10901] 20240223 ck >>  // << [LE0-18125] 20250114 Tracy >>
                (void)pecallMgr->peCallEventLog()->get_vehicle_config_mode(); // << [LE0-10901] 20240223 ck >>

                // << [LE0-18125] 20250314 EasonCCLiao
                if (beforeState == LCM_STATE_UPDATE) {
                    pecallMgr->peCallAudio()->removeDataAudioFile();
                }
                // >> [LE0-18125]
                break;
            }
            case LCM_STATE_UPDATE: {
                pecallMgr->peCallCan()->handleDysfunctionLedUpdateMode(); // << [LE0-9764] 20231117 CK >>
                pecallMgr->peCallEventLog()->notifyUpdateEvent(VEHICLE_CAN, static_cast<uint32_t>(VEHICLE_CANSTATE_COMEOFF));
                break;
            }
            default: {
                _logger.warning("%s", fmt::format("lcm state:{} out of scope!", afterState));
                break;
            }
        }
		pecallMgr->pCmdThread()->sendstringcmd(AUDIOMANAGER_TYPE, afterState, NORMAL, ""); // << [LE0-5724] 20230617 CK >>
    }
    // >> [LE0-4015]
}

void CeCallMachine::recoverLcmThermal(std::string thermal) {
        // reset lcm thermal // << [LE0-14407] 20240627 CK >> // Reset Thermal level to 0 (because cold boot not receive currently Thermal level)
        pecallMgr->peCallMachine()->resetInternalThermalMitigation();
        _logger.notice("%s", fmt::format("recoverLCMThermal thermal:{}", thermal)); // << [LE0-14407] 20240627 CK >>
        handleLcmThermal(thermal);
}

void CeCallMachine::handleLcmThermal(std::string thermal)
{
	pecallMgr->peCallMachine()->setInternalThermalMitigationValue(thermal);
	pecallMgr->peCallXmlConfig()->saveThermalMitigation(thermal); // << [LE0-10901] 20240223 ck >>
}
//>> [LE0-14407] 

// << [LE0-15588] 20240823 MaoJunYan
bool CeCallMachine::isWCBIdle() {
    bool wcbIdle = false;
    if ((eCallState::ECALL_WAITING_FOR_CALLBACK == getEcallMachineState()) && (eCallState::ECALL_WCB_ECALL_IDLE == getEcallMachineSubState()))
        wcbIdle = true;
    else
        _logger.notice("%s", fmt::format("It is not in WCB idle state !"));

    return wcbIdle;
}
// >> [LE0-15588] 20240823 MaoJunYan

} } // namespace MD::eCallMgr
