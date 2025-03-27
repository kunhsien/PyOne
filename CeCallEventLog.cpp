#include "CeCallEventLog.h"
#include "eCallManager.h"
#include "fmt/core.h"

#include <iostream>
#include "CeCallXmlConfig.h"
#include "CeCallRemoteServer.h"
#include "radiofih/fih_sdk_ecall_api.h"
#include <cstdlib> 

using Poco::JSON::Object;
using Poco::JSON::Array;
using Poco::LocalDateTime;
using Poco::DateTimeFormatter;
using Poco::DateTimeParser; 
using Poco::DateTime; 
using Poco::JSON::Parser;
using Poco::Dynamic::Var;
using Poco::format;
using Poco::DirectoryIterator;
using Poco::TimerCallback;
using Poco::Timestamp; 

namespace MD {
namespace eCallMgr {

CeCallEventLog::CeCallEventLog(eCallManager* const pMgr)
	:pecallMgr(pMgr)
    ,_logger(CLogger::newInstance("Log"))
{
    updateEvent += Poco::delegate(this, &CeCallEventLog::onUpdateEventLog);
	initEventObjects();
}

CeCallEventLog::~CeCallEventLog()
{
	try {
		updateEvent -= Poco::delegate(this, &CeCallEventLog::onUpdateEventLog);
    } catch (Poco::Exception &e) {
        _logger.error("%s", fmt::format("~CeCallEventLog failed, e::{}", e.displayText()));
    } catch (...) {
        _logger.notice("~CeCallEventLog failed!");
    }
}

void CeCallEventLog::stopTimer() {
    try {
        m_informTimer.stop();
        m_logFileExpiredTimer.stop();
    } catch (Poco::Exception& exc) {
        _logger.error("%s", fmt::format("stopTimer fail, e::{}", exc.displayText()));
    } catch (...) {
        _logger.error("%s", fmt::format("stopTimer Exception"));
    }
}
const std::string CeCallEventLog::toString(const uint32_t index, const std::string table[], uint32_t numOfElemts)
{
	std::string result{};
	if(index > (numOfElemts -1U))
	{
		_logger.error("[toString] array overlapping: index[%d] > max_element[%d] \n", index, numOfElemts);
		result = "invalid_string"; 
	}
	result = table[index];
	return result;
}

const std::string CeCallEventLog::toString(const uint32_t data, const bool is_hex)
{
   std::string result="";
   if(is_hex)
   {
        std::ostringstream tempString;
        tempString << std::hex << data;
	    result = tempString.str();
   }
   else
   {
        result = to_string(data);
   }
   return result;
}

const std::string CeCallEventLog::toString(const std::vector<uint16_t> data_set)
{
	std::string output = "";
	std::ostringstream tempString("");
	for(auto i = 0U; i< data_set.size(); i++)
    {
		tempString << std::dec << data_set[i];
	}
	output = tempString.str();
	return output;
}

void CeCallEventLog::onUpdateEventLog(std::string& updateEvent)
{
    int32_t event;
	uint32_t param_0;
	Parser p;
	Var result = p.parse(updateEvent);
	Object::Ptr pObject = result.extract<Object::Ptr>();
	event = pObject->getValue<int32_t>("event"); 
	param_0 = pObject->getValue<uint32_t>("param_0");
	
	_logger.information("%s", fmt::format("[onUpdateEventLog] receive event:{}",event));
	switch (event)
	{
		case CAN_INFO_CRASH_CONTENT:
		{
			updateCanInfoCrashContent(param_0);
			break;
		}

		case CELLULAR_NETWORK_STATE:
		{
			m_sEventLog.getCellularNetworkLog().setTime(get_unix_time());
            const uint32_t i16ErrorCode = param_0;
			const uint32_t eSrvState = pObject->getValue<uint32_t>("param_1");
			updateCellularNetworkState(i16ErrorCode,eSrvState);
			break;
		}

		case CELLULAR_NETWORK_CELL:
		{
			break;
		}

		case ECALL_SESSION_STATE:
		{
			_logger.information("[onUpdateEventLog]:ECALL_SESSION_STATE"); 
		
			m_sEventLog.getEcallSessionLog().setState(toString(param_0,Enum_eCall_session_state, NUM_OF_ELEMENTS(Enum_eCall_session_state)));
			_logger.notice("%s", fmt::format("[onUpdateEventLog]:ECALL_SESSION_STATE param_0:{} isSessionStart():{}", param_0, isSessionStart()));
			pecallMgr->peCallRemoteServer()->notifyLog(Enum_eCall_session_state[param_0]);
			if(static_cast<uint32_t>(ECALL_SESSION_STATE_STARTED) == param_0) 
			{
				const uint32_t ecall_mode = pObject->getValue<uint32_t>("param_1");
				const std::string ecall_number = pObject->getValue<std::string>("param_2");
				updateECallSessionStateStarted(ecall_mode, ecall_number);
			}
			else if(static_cast<uint32_t>(ECALL_SESSION_STATE_STOPPED_OK) == param_0)
			{
			    endSession();
			}
			else
			{
				updateEventLog(m_sEventLog.getEcallSessionLog());

			}
			break;
		}

        case GNSS_FIX_FIX:
		{
			break;
		}

        case LOGFILE_SAVING_ATTEMPT_STATE:
		{
			break;
		}

        case MSD_TRANSMISSION_STATE:
		{
			updateMsdTransmissionState(param_0);
			break;
		}

		case PSAP_CALL_STATE:
		{
			updatePSAPCallState(param_0);
			break;
		}

		case TCU_POWER:
		{
			updateTCUPower(param_0);
			break;
		}

		case VEHICLE_SEV:
		{
			updateVEHICLE_SEV(param_0);
			break;
		}

		case VEHICLE_CAN:
		{
			updateVEHICLE_CAN(param_0);
			break;
		}

		case FILE_EXPIRED_TIME_OUT: 
		{
			_logger.notice("%s", fmt::format("[onUpdateEventLog]:handleTimeOut param_0:{}", param_0));
			handleTimeOut(E_LOG_FILE_EXPIRED_TIME_OUT); // << [LE022-5418] 20241024 EasonCCLiao >>
			break;
		}

		case CONFIG_PARAM_READY:
		{
		   _logger.notice("%s", fmt::format("[onUpdateEventLog]CONFIG_PARAM_READY"));
           buildLogFiles();
		   break;
		}

		default:
            break;
	}
}

void CeCallEventLog::notifyUpdateEvent(const int32_t event, const uint32_t param_0)
{
	_logger.information("[notifyUpdateEvent0]");
    Object::Ptr command = new Object();
    (void)command->set("event",event);
    (void)command->set("param_0", param_0);
	std::stringstream ss_str;
	command->stringify(ss_str);
	std::string s_str = ss_str.str();
	updateEvent.notify(this, s_str);
}

void CeCallEventLog::notifyUpdateEvent(const int32_t event, const uint32_t param_0, const uint32_t param_1)
{
	_logger.information("[notifyUpdateEvent1]");
    Object::Ptr command = new Object();
    (void)command->set("event",event);
    (void)command->set("param_0", param_0);
	(void)command->set("param_1", param_1);
	std::stringstream ss_str;
	command->stringify(ss_str);
	std::string s_str = ss_str.str();
	updateEvent.notify(this, s_str);
}

void CeCallEventLog::notifyUpdateEvent(const int32_t event, const uint32_t param_0, const uint32_t param_1, const uint32_t param_2) 
{
	_logger.information("[notifyUpdateEvent2]");
    Object::Ptr command = new Object();
    (void)command->set("event",event);
    (void)command->set("param_0", param_0);
	(void)command->set("param_1", param_1);
	(void)command->set("param_2", param_2);
	std::stringstream ss_str;
	command->stringify(ss_str);
	std::string s_str = ss_str.str();
	updateEvent.notify(this, s_str);
}

void CeCallEventLog::notifyUpdateEvent(const int32_t event, const int32_t param_0, const uint32_t param_1, const std::string param_2) 
{
	_logger.information("[notifyUpdateEvent3]");
    Object::Ptr command = new Object();
    (void)command->set("event",event);
    (void)command->set("param_0", param_0);
	(void)command->set("param_1", param_1);
	(void)command->set("param_2", param_2);
	std::stringstream ss_str;
	command->stringify(ss_str);
	std::string s_str = ss_str.str();
	updateEvent.notify(this, s_str);
}

void CeCallEventLog::initEventObjects()
{
	_logger.information("[initEventObjects]"); 
    m_sEventLog.getProp().setId(0);
	m_sEventLog.getProp().setUsage("eCall 112 Reglementation Europe");
	m_sEventLog.getProp().setUrl("");//server out of scope
	m_sEventLog.getProp().setRetain(0);//server out of scope
	m_sEventLog.getProp().setVersion("v0.2");
	 

	m_sEventLog.getCANinfoCrashLog().setTime(0);
	m_sEventLog.getCANinfoCrashLog().setType("CAN_INFO_CRASH");
	m_sEventLog.getCANinfoCrashLog().setContent("");
	

	m_sEventLog.getCellularNetworkLog().setTime(0);
	m_sEventLog.getCellularNetworkLog().setType("CELL_NETWORK_REG");
	m_sEventLog.getCellularNetworkLog().setState("");
	m_sEventLog.getCellularNetworkLog().getCell().setId(0);
    m_sEventLog.getCellularNetworkLog().getCell().setName("");
	m_sEventLog.getCellularNetworkLog().getCell().setMcc(0);
	m_sEventLog.getCellularNetworkLog().getCell().setMnc(0);
	m_sEventLog.getCellularNetworkLog().getCell().setRat("");
	m_sEventLog.getCellularNetworkLog().getCell().setRxlevel(0);
	/*signal cell rat FIH_ECALL_URC_CELLULAR_NET_SIGNAL_STRENGTH_IND*/
	m_sEventLog.getCellularNetworkLog().setData("");
	
	m_sEventLog.getEcallSessionLog().setTime(0);
	m_sEventLog.getEcallSessionLog().setType("ECALL_SESSION");
	m_sEventLog.getEcallSessionLog().setState("");
	m_sEventLog.getEcallSessionLog().setOdometer(0);//get_vehicle_odometer
	m_sEventLog.getEcallSessionLog().setTcuTemp(0);
	
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().setState("");//FIH_ECALL_GET_VOICE_REG_RSP
	
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().setId(0);
    m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().setName("");//get_Name_of_the_operator()
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().setMcc(0);
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().setMnc(0);
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().setRat("");
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().setRxlevel(0);
	/*signal cell rat FIH_ECALL_URC_CELLULAR_NET_SIGNAL_STRENGTH_IND*/
	//GSM signal
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().GSM_signal.setSs(0);
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().GSM_signal.setBer(0);

	//WCDMA signal
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().UMTS_signal.setSs(0);
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().UMTS_signal.setBer(0);
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().UMTS_signal.setEcio(0);
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().UMTS_signal.setRscp(0);
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().UMTS_signal.setSinr(0);

	//LTE signal
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().LTE_signal.setSs(0);
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().LTE_signal.setBer(0);
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().LTE_signal.setRsrq(0);
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().LTE_signal.setRsrp(0);
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().LTE_signal.setSnr(0);
	
	/*cellOthers cellnet state*/
	
	m_sEventLog.getEcallSessionLog().getInit().getEcall().setTrigger("");
	m_sEventLog.getEcallSessionLog().getInit().getEcall().setMode("");//ecall_variant
	m_sEventLog.getEcallSessionLog().getInit().getEcall().setPsapNumber("");//cAddress
	m_sEventLog.getEcallSessionLog().getInit().getEcall().setInfoCrashFrame(0U);//Frame_ETAT_INFO_CRASH INFO_CRASH

	m_sEventLog.getEcallSessionLog().getInit().getGnss().setFix("");
	m_sEventLog.getEcallSessionLog().getInit().getGnss().setSystem("");
	m_sEventLog.getEcallSessionLog().getInit().getGnss().getDop().p = 0U;
	m_sEventLog.getEcallSessionLog().getInit().getGnss().getDop().h = 0U;
	m_sEventLog.getEcallSessionLog().getInit().getGnss().getDop().v = 0U;
	m_sEventLog.getEcallSessionLog().getInit().getGnss().getSat().snr = "";
	m_sEventLog.getEcallSessionLog().getInit().getGnss().getErr().hErr = 0U;

	m_sEventLog.getEcallSessionLog().getInit().getTcu().setImei("");//get_International_Mobile_Equipment_Identity
	m_sEventLog.getEcallSessionLog().getInit().getTcu().setPower("");//XCALL_VEHICLE_BATT_NOTIFY XCALL_BACKUP_BATT_NOTIFY
	m_sEventLog.getEcallSessionLog().getInit().getTcu().setHwRef("");//get_product_number
	m_sEventLog.getEcallSessionLog().getInit().getTcu().setSwVersionTel("");//get_Identification_Zone_for_Downloadable_ECU_service
	m_sEventLog.getEcallSessionLog().getInit().getTcu().setSwEditionTel("");//get_Identification_Zone_for_Downloadable_ECU_service
	m_sEventLog.getEcallSessionLog().getInit().getTcu().setSwFile("");//get_Identification_Zone_for_Downloadable_ECU_service
	m_sEventLog.getEcallSessionLog().getInit().getTcu().setRxSwin("");

	m_sEventLog.getEcallSessionLog().getInit().getVeh().setCanState("");
	m_sEventLog.getEcallSessionLog().getInit().getVeh().setSev("");
	m_sEventLog.getEcallSessionLog().getInit().getVeh().setConfig("");

	m_sEventLog.getEcallSessionLog().setData("");


	m_sEventLog.getGNSSFixLog().setTime(0);
	m_sEventLog.getGNSSFixLog().setType("GNSS_FIX");
	m_sEventLog.getGNSSFixLog().setFix("");
	m_sEventLog.getGNSSFixLog().setSystem("");
	m_sEventLog.getGNSSFixLog().getDop().p = 0U;
	m_sEventLog.getGNSSFixLog().getDop().h = 0U;
	m_sEventLog.getGNSSFixLog().getDop().v = 0U;
	m_sEventLog.getGNSSFixLog().getSat().snr = "";
	m_sEventLog.getGNSSFixLog().getErr().hErr = 0U;

	
	m_sEventLog.getLogfileSavingAttemptLog().setTime(0);
	m_sEventLog.getLogfileSavingAttemptLog().setType("LOGFILE_SAVING_ATTEMPT");
	m_sEventLog.getLogfileSavingAttemptLog().setState("SAVING");
	m_sEventLog.getLogfileSavingAttemptLog().setHttpStatusCode("0");//server out of scope


	m_sEventLog.getMSDTransmissionLog().setTime(0);
    m_sEventLog.getMSDTransmissionLog().setType("MSD");
	m_sEventLog.getMSDTransmissionLog().setState("");//msd_state
	m_sEventLog.getMSDTransmissionLog().setSource("GNSS");
	
	m_sEventLog.getMSDTransmissionLog().getGnss().setFix("");
	m_sEventLog.getMSDTransmissionLog().getGnss().setSystem("");
	m_sEventLog.getMSDTransmissionLog().getGnss().getDop().p = 0U;
	m_sEventLog.getMSDTransmissionLog().getGnss().getDop().h = 0U;
	m_sEventLog.getMSDTransmissionLog().getGnss().getDop().v = 0U;
	m_sEventLog.getMSDTransmissionLog().getGnss().getSat().snr = "";
	m_sEventLog.getMSDTransmissionLog().getGnss().getErr().hErr = 0U;
	
	m_sEventLog.getMSDTransmissionLog().setData("");


	m_sEventLog.getPSAPCallLog().setTime(0);
    m_sEventLog.getPSAPCallLog().setType("PSAP_CALL");
	m_sEventLog.getPSAPCallLog().setState("");//call_sub_state
	m_sEventLog.getPSAPCallLog().setData("");


	m_sEventLog.getTCULog().setTime(0);
	m_sEventLog.getTCULog().setType("TCU");
	m_sEventLog.getTCULog().setPower(""); //XCALL_VEHICLE_BATT_NOTIFY XCALL_BACKUP_BATT_NOTIFY


	m_sEventLog.getVehicleLog().setTime(0);
    m_sEventLog.getVehicleLog().setType("VEH");
	m_sEventLog.getVehicleLog().setSev("");
	m_sEventLog.getVehicleLog().setCanState("");
}

void CeCallEventLog::initEventObjectsForNewSession()
{
    m_sEventLog.getProp().setId(static_cast<int32_t>(m_sessionId));
	_logger.notice("%s", fmt::format("[initEventObjectsForNewSession] m_sEventLog.prop.id:{}", m_sEventLog.getProp().getId()));
	m_sEventLog.getProp().setUsage("eCall 112 Reglementation Europe");
	m_sEventLog.getProp().setUrl("");//server out of scope
	m_sEventLog.getProp().setRetain(0);//server out of scope
	m_sEventLog.getProp().setVersion("v0.2");
	
	m_sEventLog.getEcallSessionLog().setTime(get_unix_time());
	m_sEventLog.getEcallSessionLog().setType("ECALL_SESSION");
	m_sEventLog.getEcallSessionLog().setOdometer(static_cast<int32_t>(get_vehicle_odometer()));
	m_sEventLog.getEcallSessionLog().setTcuTemp(0);
   
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().setState(m_sEventLog.getCellularNetworkLog().getState());
	
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().setId(m_sEventLog.getCellularNetworkLog().getCell().getId());
    m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().setName(m_sEventLog.getCellularNetworkLog().getCell().getName());
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().setMcc(m_sEventLog.getCellularNetworkLog().getCell().getMcc());
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().setMnc(m_sEventLog.getCellularNetworkLog().getCell().getMnc());
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().setRat(m_sEventLog.getCellularNetworkLog().getCell().getRat());
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().setRxlevel(m_sEventLog.getCellularNetworkLog().getCell().getRxlevel());
	/*signal cell rat FIH_ECALL_URC_CELLULAR_NET_SIGNAL_STRENGTH_IND*/
	//GSM signal
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().GSM_signal.setSs(m_sEventLog.getCellularNetworkLog().getSignal().GSM_signal.getSs());
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().GSM_signal.setBer(m_sEventLog.getCellularNetworkLog().getSignal().GSM_signal.getBer());

	//WCDMA signal
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().UMTS_signal.setSs(m_sEventLog.getCellularNetworkLog().getSignal().UMTS_signal.getSs());
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().UMTS_signal.setBer(m_sEventLog.getCellularNetworkLog().getSignal().UMTS_signal.getBer());
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().UMTS_signal.setEcio(m_sEventLog.getCellularNetworkLog().getSignal().UMTS_signal.getEcio());
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().UMTS_signal.setRscp(m_sEventLog.getCellularNetworkLog().getSignal().UMTS_signal.getRscp());
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().UMTS_signal.setSinr(m_sEventLog.getCellularNetworkLog().getSignal().UMTS_signal.getSinr());

	//LTE signal
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().LTE_signal.setSs(m_sEventLog.getCellularNetworkLog().getSignal().LTE_signal.getSs());
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().LTE_signal.setBer(m_sEventLog.getCellularNetworkLog().getSignal().LTE_signal.getBer());
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().LTE_signal.setRsrq(m_sEventLog.getCellularNetworkLog().getSignal().LTE_signal.getRsrq());
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().LTE_signal.setRsrp(m_sEventLog.getCellularNetworkLog().getSignal().LTE_signal.getRsrp());
	m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().LTE_signal.setSnr(m_sEventLog.getCellularNetworkLog().getSignal().LTE_signal.getSnr());
	
	/*cellOthers cellnet state*/
    
	m_sEventLog.getEcallSessionLog().getInit().getEcall().setTrigger(m_sEventLog.getEcallSessionLog().getInit().getEcall().getTrigger());
	m_sEventLog.getEcallSessionLog().getInit().getEcall().setInfoCrashFrame(get_info_crash());//Frame_ETAT_INFO_CRASH INFO_CRASH

	getGnssInitData(m_sEventLog.getEcallSessionLog().getInit().getGnss());

    DATA_IdentificationZone data;
	get_Identification_Zone_for_Downloadable_ECU_service(data);

	m_sEventLog.getEcallSessionLog().getInit().getTcu().setImei(get_International_Mobile_Equipment_Identity());
	m_sEventLog.getEcallSessionLog().getInit().getTcu().setPower(m_sEventLog.getTCULog().getPower());//XCALL_VEHICLE_BATT_NOTIFY XCALL_BACKUP_BATT_NOTIFY
	m_sEventLog.getEcallSessionLog().getInit().getTcu().setHwRef(get_product_number());

	m_sEventLog.getEcallSessionLog().getInit().getTcu().setSwVersionTel(toString(data.SW_VER,true)); //get_Identification_Zone_for_Downloadable_ECU_service
	m_sEventLog.getEcallSessionLog().getInit().getTcu().setSwEditionTel(toString(data.SW_EDITION,true));//get_Identification_Zone_for_Downloadable_ECU_service
	m_sEventLog.getEcallSessionLog().getInit().getTcu().setSwFile(toString(static_cast<uint32_t>(data.SW_REFERENCE[0])<<16|static_cast<uint32_t>(data.SW_REFERENCE[1])<<8|data.SW_REFERENCE[2],true));//get_Identification_Zone_for_Downloadable_ECU_service
	
	m_sEventLog.getEcallSessionLog().getInit().getTcu().setRxSwin(get_regulation_xswids_num());
    
	_logger.information("%s", fmt::format("[initEventObjects]:m_sEventLog.Vehicle_log.canState:{}", m_sEventLog.getVehicleLog().getCanState()));
	m_sEventLog.getEcallSessionLog().getInit().getVeh().setCanState(m_sEventLog.getVehicleLog().getCanState());
	m_sEventLog.getEcallSessionLog().getInit().getVeh().setSev(m_sEventLog.getVehicleLog().getSev());
	m_sEventLog.getEcallSessionLog().getInit().getVeh().setConfig(get_vehicle_config_mode());

	m_sEventLog.getEcallSessionLog().setData("");

}

void CeCallEventLog::createInitialEventLog() 
{
	_logger.information("[createInitialEventLog]"); 
	Object::Ptr prop = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
	(void)prop->set("id", m_sEventLog.getProp().getId());
	(void)prop->set("usage", m_sEventLog.getProp().getUsage());
	(void)prop->set("url", m_sEventLog.getProp().getUrl());
	(void)prop->set("version", m_sEventLog.getProp().getVersion());

	Object::Ptr ecall_session_init_cellnet_cell = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
	(void)ecall_session_init_cellnet_cell->set("id", m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().getId());
    (void)ecall_session_init_cellnet_cell->set("name", m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().getName());
	(void)ecall_session_init_cellnet_cell->set("mcc", m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().getMcc());
	(void)ecall_session_init_cellnet_cell->set("mnc", m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().getMnc());
	(void)ecall_session_init_cellnet_cell->set("rat", m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().getRat());
	(void)ecall_session_init_cellnet_cell->set("rxlevel", m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().getRxlevel());

	Object::Ptr ecall_session_init_cellnet = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
	(void)ecall_session_init_cellnet->set("state", m_sEventLog.getEcallSessionLog().getInit().getCellnet().getState());
	(void)ecall_session_init_cellnet->set("cell", ecall_session_init_cellnet_cell);

	//signal to be added is json object
	if(m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().getRat() == "GSM")
	{
		Object::Ptr ecall_session_init_cellnet_signal_gsm = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)ecall_session_init_cellnet_signal_gsm->set("ss", m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().GSM_signal.getSs());
		(void)ecall_session_init_cellnet_signal_gsm->set("ber", m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().GSM_signal.getBer());

		(void)ecall_session_init_cellnet->set("signal", ecall_session_init_cellnet_signal_gsm);
	} 
	else if(m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().getRat() == "WCDMA")
	{
		Object::Ptr ecall_session_init_cellnet_signal_wcdma = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)ecall_session_init_cellnet_signal_wcdma->set("ss", m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().UMTS_signal.getSs());
		(void)ecall_session_init_cellnet_signal_wcdma->set("ber", m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().UMTS_signal.getBer());
		(void)ecall_session_init_cellnet_signal_wcdma->set("ecio", m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().UMTS_signal.getEcio());
		(void)ecall_session_init_cellnet_signal_wcdma->set("rscp", m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().UMTS_signal.getRscp());

		(void)ecall_session_init_cellnet->set("signal", ecall_session_init_cellnet_signal_wcdma);
	}
	else if(m_sEventLog.getEcallSessionLog().getInit().getCellnet().getCell().getRat() == "LTE")
	{
		Object::Ptr ecall_session_init_cellnet_signal_lte = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)ecall_session_init_cellnet_signal_lte->set("ss", m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().LTE_signal.getSs());
		(void)ecall_session_init_cellnet_signal_lte->set("rsrq", m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().LTE_signal.getRsrq());
		(void)ecall_session_init_cellnet_signal_lte->set("rsrp", m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().LTE_signal.getRsrp());
		(void)ecall_session_init_cellnet_signal_lte->set("snr", m_sEventLog.getEcallSessionLog().getInit().getCellnet().getSignal().LTE_signal.getSnr());

		(void)ecall_session_init_cellnet->set("signal", ecall_session_init_cellnet_signal_lte);
	}
	else
	{
		//Do nothing
	}

	//cellOthers to be added is json object

	Object::Ptr ecall_session_init_ecall = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
	(void)ecall_session_init_ecall->set("trigger", m_sEventLog.getEcallSessionLog().getInit().getEcall().getTrigger());
	(void)ecall_session_init_ecall->set("mode", m_sEventLog.getEcallSessionLog().getInit().getEcall().getMode());
	(void)ecall_session_init_ecall->set("psapNumber", m_sEventLog.getEcallSessionLog().getInit().getEcall().getPsapNumber());
	(void)ecall_session_init_ecall->set("infoCrashFrame", m_sEventLog.getEcallSessionLog().getInit().getEcall().getInfoCrashFrame());

    Object::Ptr ecall_session_init_gnss_dop = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
    (void)ecall_session_init_gnss_dop->set("p", m_sEventLog.getEcallSessionLog().getInit().getGnss().getDop().p);
	(void)ecall_session_init_gnss_dop->set("h", m_sEventLog.getEcallSessionLog().getInit().getGnss().getDop().h);
	(void)ecall_session_init_gnss_dop->set("v", m_sEventLog.getEcallSessionLog().getInit().getGnss().getDop().v);
	
	Object::Ptr ecall_session_init_gnss_sat = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
	(void)ecall_session_init_gnss_sat->set("snr", m_sEventLog.getEcallSessionLog().getInit().getGnss().getSat().snr);

	Object::Ptr ecall_session_init_gnss_err = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
	(void)ecall_session_init_gnss_err->set("hErr", m_sEventLog.getEcallSessionLog().getInit().getGnss().getErr().hErr);

	Object::Ptr ecall_session_init_gnss = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
	(void)ecall_session_init_gnss->set("fix", m_sEventLog.getEcallSessionLog().getInit().getGnss().getFix());
	(void)ecall_session_init_gnss->set("system", m_sEventLog.getEcallSessionLog().getInit().getGnss().getSystem());
	(void)ecall_session_init_gnss->set("dop", ecall_session_init_gnss_dop);   
	(void)ecall_session_init_gnss->set("sat", ecall_session_init_gnss_sat);
	(void)ecall_session_init_gnss->set("err", ecall_session_init_gnss_err);

    Object::Ptr ecall_session_init_tcu = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
	(void)ecall_session_init_tcu->set("imei", m_sEventLog.getEcallSessionLog().getInit().getTcu().getImei());
	(void)ecall_session_init_tcu->set("power", m_sEventLog.getEcallSessionLog().getInit().getTcu().getPower());
	(void)ecall_session_init_tcu->set("hwRef", m_sEventLog.getEcallSessionLog().getInit().getTcu().getHwRef());
	(void)ecall_session_init_tcu->set("swVersionTel", m_sEventLog.getEcallSessionLog().getInit().getTcu().getSwVersionTel());
	(void)ecall_session_init_tcu->set("swEditionTel", m_sEventLog.getEcallSessionLog().getInit().getTcu().getSwEditionTel());
	(void)ecall_session_init_tcu->set("swFile", m_sEventLog.getEcallSessionLog().getInit().getTcu().getSwFile());
	(void)ecall_session_init_tcu->set("rxSwin", m_sEventLog.getEcallSessionLog().getInit().getTcu().getRxSwin());

	Object::Ptr ecall_session_init_veh = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
	(void)ecall_session_init_veh->set("canState", m_sEventLog.getEcallSessionLog().getInit().getVeh().getCanState());
	(void)ecall_session_init_veh->set("sev", m_sEventLog.getEcallSessionLog().getInit().getVeh().getSev());
	(void)ecall_session_init_veh->set("config", m_sEventLog.getEcallSessionLog().getInit().getVeh().getConfig());

	Object::Ptr ecall_session_init = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
	(void)ecall_session_init->set("cellnet", ecall_session_init_cellnet);
	(void)ecall_session_init->set("ecall", ecall_session_init_ecall);
	(void)ecall_session_init->set("gnss", ecall_session_init_gnss);
	(void)ecall_session_init->set("tcu", ecall_session_init_tcu);
	(void)ecall_session_init->set("veh", ecall_session_init_veh);

	Object::Ptr ecall_session_log = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
	(void)ecall_session_log->set("time", m_sEventLog.getEcallSessionLog().getTime());
	(void)ecall_session_log->set("type", m_sEventLog.getEcallSessionLog().getType());
	(void)ecall_session_log->set("state", m_sEventLog.getEcallSessionLog().getState());
	(void)ecall_session_log->set("odometer", m_sEventLog.getEcallSessionLog().getOdometer());
	(void)ecall_session_log->set("tcuTemp", m_sEventLog.getEcallSessionLog().getTcuTemp());
	(void)ecall_session_log->set("init", ecall_session_init);
    
	collect_event(ecall_session_log);

	update();	 
}

NAD_RIL_Errno CeCallEventLog::getCellNetworkCellData(sEcall_Logfile_Cellular_network& cellnet)
{
	NAD_RIL_Errno result;
	nad_network_cellinfo_list_t cell_info_list;
    NAD_RIL_Errno ret = get_cell_info_list(cell_info_list);
    if(ret != NAD_E_SUCCESS)
	{
		_logger.error("%s", fmt::format("[getCellNetworkCellData] unsuccess ret:{}", ret));
        result = ret;
	}
	else 
	{
		cellnet.getCell().setName(get_Name_of_the_operator());
		_logger.information("%s", fmt::format("[getCellNetworkCellData] cell.name:{}", cellnet.getCell().getName()));

		if(cell_info_list.cellsInfo[0].registered != 0)
		{
			sEcall_Cellular_network_Cell& cell = cellnet.getCell();
			uEcall_Cellular_network_signal& signal = cellnet.getSignal();
			switch(cell_info_list.cellsInfo[0].cellInfoType)
			{
				case CELL_INFO_TYPE_GSM:
				{
					//cell
					cell.setRat("GSM");
					cell.setId(cell_info_list.cellsInfo[0].CellInfo.gsm.cellIdentityGsm.cid);
					cell.setMcc(cell_info_list.cellsInfo[0].CellInfo.gsm.cellIdentityGsm.mcc);
					cell.setMnc(cell_info_list.cellsInfo[0].CellInfo.gsm.cellIdentityGsm.mnc);
					cell.setRxlevel(cell_info_list.cellsInfo[0].CellInfo.gsm.signalStrengthGsm.signalStrength);

					//signal
					signal.GSM_signal.setSs(cell_info_list.cellsInfo[0].CellInfo.gsm.signalStrengthGsm.signalStrength);
					signal.GSM_signal.setBer(cell_info_list.cellsInfo[0].CellInfo.gsm.signalStrengthGsm.bitErrorRate);
					break;					
				}

				case CELL_INFO_TYPE_WCDMA:
				{
					//cell
					cell.setRat("WCDMA");
					cell.setId(cell_info_list.cellsInfo[0].CellInfo.wcdma.cellIdentityWcdma.cid);
					cell.setMcc(cell_info_list.cellsInfo[0].CellInfo.wcdma.cellIdentityWcdma.mcc);
					cell.setMnc(cell_info_list.cellsInfo[0].CellInfo.wcdma.cellIdentityWcdma.mnc);
					cell.setRxlevel(cell_info_list.cellsInfo[0].CellInfo.wcdma.signalStrengthWcdma.signalStrength);

					//signal
					signal.UMTS_signal.setSs(cell_info_list.cellsInfo[0].CellInfo.wcdma.signalStrengthWcdma.signalStrength);
					signal.UMTS_signal.setBer(cell_info_list.cellsInfo[0].CellInfo.wcdma.signalStrengthWcdma.bitErrorRate);
					signal.UMTS_signal.setRscp(cell_info_list.cellsInfo[0].CellInfo.wcdma.signalStrengthWcdma.rscp);
					signal.UMTS_signal.setEcio(cell_info_list.cellsInfo[0].CellInfo.wcdma.signalStrengthWcdma.ecno);
					break;					
				}

				case CELL_INFO_TYPE_LTE:
				{
					//cell
					cell.setRat("LTE");
					cell.setId(cell_info_list.cellsInfo[0].CellInfo.lte.cellIdentityLte.ci);
					cell.setMcc(cell_info_list.cellsInfo[0].CellInfo.lte.cellIdentityLte.mcc);
					cell.setMnc(cell_info_list.cellsInfo[0].CellInfo.lte.cellIdentityLte.mnc);
					cell.setRxlevel(cell_info_list.cellsInfo[0].CellInfo.lte.signalStrengthLte.signalStrength);

					//signal
					signal.LTE_signal.setSs(cell_info_list.cellsInfo[0].CellInfo.lte.signalStrengthLte.signalStrength);
					signal.LTE_signal.setRsrp(cell_info_list.cellsInfo[0].CellInfo.lte.signalStrengthLte.rsrp);
					signal.LTE_signal.setRsrq(cell_info_list.cellsInfo[0].CellInfo.lte.signalStrengthLte.rsrq);
					signal.LTE_signal.setSnr(cell_info_list.cellsInfo[0].CellInfo.lte.signalStrengthLte.rssnr);
					break;					
				}

				default:
					break;
			}
		}
		result = NAD_E_SUCCESS;
	}
	return result;
}

void CeCallEventLog::getGnssData(sEcall_Logfile_GNSS_fix& gnss)
{
	_logger.information("getGnssData");
	gnss.setTime(get_unix_time());
	gnss.setType("GNSS_FIX");
	gnss.setFix((gnss_test_fix == "") ? pecallMgr->pDataStore()->getPosition_fixstatus():gnss_test_fix);
	gnss.setSystem(pecallMgr->pDataStore()->getPosition_system());
	gnss.getDop().p = pecallMgr->pDataStore()->getPosition_Pdop();
	gnss.getDop().h = pecallMgr->pDataStore()->getPosition_Hdop();
	gnss.getDop().v = pecallMgr->pDataStore()->getPosition_Vdop();
	gnss.getSat().snr = toString(pecallMgr->pDataStore()->getPosition_used_snr());
	gnss.getErr().hErr = pecallMgr->pDataStore()->getPosition_Sigma_h_position();
}

void CeCallEventLog::getGnssInitData(sEcall_eCall_session_GNSS_init& gnss)
{
	_logger.information("getGnssInitData");
	gnss.setFix((gnss_test_fix == "") ? pecallMgr->pDataStore()->getPosition_fixstatus():gnss_test_fix);
	gnss.setSystem(pecallMgr->pDataStore()->getPosition_system());
	gnss.getDop().p = pecallMgr->pDataStore()->getPosition_Pdop();
	gnss.getDop().h = pecallMgr->pDataStore()->getPosition_Hdop();
	gnss.getDop().v = pecallMgr->pDataStore()->getPosition_Vdop();
	gnss.getSat().snr = toString(pecallMgr->pDataStore()->getPosition_used_snr());
	gnss.getErr().hErr = pecallMgr->pDataStore()->getPosition_Sigma_h_position();

	_logger.information("%s", fmt::format("getPosition_fixstatus:{}", pecallMgr->pDataStore()->getPosition_fixstatus()));
	_logger.information("%s", fmt::format("getPosition_system:{}", pecallMgr->pDataStore()->getPosition_system()));
	_logger.information("%s", fmt::format("getPosition_Pdop:{}", pecallMgr->pDataStore()->getPosition_Pdop()));
	_logger.information("%s", fmt::format("getPosition_Hdop:{}", pecallMgr->pDataStore()->getPosition_Hdop()));
	_logger.information("%s", fmt::format("getPosition_Vdop:{}", pecallMgr->pDataStore()->getPosition_Vdop()));
	_logger.information("%s", fmt::format("getPosition_used_snr:{}", toString(pecallMgr->pDataStore()->getPosition_used_snr())));
	_logger.information("%s", fmt::format("getPosition_Sigma_h_position:{}", pecallMgr->pDataStore()->getPosition_Sigma_h_position()));	
}

void CeCallEventLog::store(Object::Ptr event)
{
	if(event)
	{
		std::stringstream ss_str;
		event->stringify(ss_str, 4U);
		std::string s_str = ss_str.str();
		_logger.notice("%s", fmt::format("[store] event json:{}", s_str.c_str()));

		const Poco::Path filePath(m_folderPath, m_fileName);
		if (filePath.isFile())
		{
			Poco::File outputFile(filePath);
			if ( outputFile.exists() )
			{
				Poco::FileStream fos(filePath.toString());
				(void)fos.seekp(0, std::ios::beg);
				fos << std::endl;
				(void)Poco::StreamCopier::copyStream(ss_str, fos);
				fos.close();
			}
			else
			{
				_logger.notice("%s", fmt::format("[store] create event log file:{}", filePath.toString()));
				(void)outputFile.createFile();
				Poco::FileStream fos(filePath.toString());
				(void)Poco::StreamCopier::copyStream(ss_str, fos);
				fos.close();
			}
		}
		else
		{
			_logger.error("%s", fmt::format("[store] The path is not a file: {}", filePath.toString()));
		}
	}
	else
	{
		_logger.error("%s", fmt::format("[store] event is null"));
	}

	
}

//<< [LE0-9065] TracyTu 
bool CeCallEventLog::isAllowedToRemove(const std::string& path)
{
	bool canRemove = true;
    if(m_SessionState != E_SESSION_STATE_STOP)
	{
		const Poco::Path filePath(m_folderPath, m_fileName);
        canRemove = (filePath.toString().compare(path) != 0) ? true : false;
	}
    _logger.notice("%s", fmt::format("[isAllowedToRemove]canRemove:{}, m_SessionState:{}, path:{}", canRemove, m_SessionState, path.c_str()));

	return canRemove;
}
//>> [LE0-9065] 

void CeCallEventLog::delExpiredLogFile()
{
	_logger.notice("%s", fmt::format("delExpiredLogFile"));

	std::sort(m_logFileCtxs.begin(), m_logFileCtxs.end(),CompareFunc);
	
	const LocalDateTime now;
	const Timestamp ts_now = now.timestamp();
	auto iter = m_logFileCtxs.begin();
	while(iter != m_logFileCtxs.end()){
		int64_t diff = ts_now.epochTime() - (*iter).m_time.timestamp().epochTime(); 
		string localTime(DateTimeFormatter::format((*iter).m_time, "%Y_%m_%d_%H_%M_%S_%i"));
		_logger.information("%s", fmt::format("[delExpiredLogFile]m_name:{}, m_time:{}, diff: {}", (*iter).m_name.c_str(), localTime.c_str(), diff));

		//<< [LE0-9065] TracyTu 	
		if(diff >= MAXIMUM_EVENT_LOG_FILE_VALIDTY_DURATION_S())
		{
			_logger.notice("%s", fmt::format("[delExpiredLogFile]del expired File:m_folderPath:{}, m_name:{}", m_folderPath.c_str(), (*iter).m_name.c_str()));
			const Poco::Path p(m_folderPath, (*iter).m_name);
			Poco::File eventFile(p);

			if(isAllowedToRemove(p.toString()))    
			{
				if (eventFile.exists())
				{
					_logger.notice("%s", fmt::format("[delExpiredLogFile]remove success!"));
					eventFile.remove(); 				
				}

				(void)m_logFileCtxs.erase(iter);

				if(m_logFileCtxs.empty()) {
					_logger.notice(fmt::format("[delExpiredLogFile]m_logFileCtxs.empty"));
					break;
				}

				iter = m_logFileCtxs.begin();
				continue;
			}
			else
			{
				_logger.notice("%s", fmt::format("[delExpiredLogFile]not allow to remove, failure!"));
			}
		}
		else
		{
			_logger.notice("%s", fmt::format("[delExpiredLogFile] not timeout, break!"));
			break;
		}
		iter++;
		//>> [LE0-9065] 
	}
}

void CeCallEventLog::logFileExpiredTimerCallBack(Poco::Timer&)
{
	_logger.information("logFileExpiredTimerCallBack!"); 
	notifyUpdateEvent(FILE_EXPIRED_TIME_OUT, static_cast<uint32_t>(E_LOG_FILE_EXPIRED_TIME_OUT));
}

void CeCallEventLog::handleTimeOut(E_TimeOut_REASON reason)
{
	_logger.information("%s", fmt::format("handleTimeOut, reason:{}", reason));
    switch(reason)
	{
		case E_LOG_FILE_EXPIRED_TIME_OUT:
			handleLogFileExpiredTimeOut();
			break;
	}   
}

void CeCallEventLog::handleLogFileExpiredTimeOut()
{
    _logger.information("hanldeLogFileExpiredTimeOut!"); 
	
	delExpiredLogFile();
    
	startLogFileExpired(true);  
}

void CeCallEventLog::startLogFileExpired(const bool isCalledInTimeOutCallBack)
{	
	_logger.notice("startLogFileExpired!"); 

   	if(m_logFileCtxs.empty())
	{
       _logger.information("[startLogFileExpired]m_logFileCtxs is empty, expired timer is not triggered!!!!");
       // << [LE022-5418] 20241024 EasonCCLiao
       // Use restart(0) to stop the timer within the callback to avoid deadlock.
       // https://docs.pocoproject.org/current/Poco.Timer.html
       m_logFileExpiredTimer.restart(0);
       // >> [LE022-5418]
	}
	else
	{
		_logger.information("[startLogFileExpired]start timer!!!!");
	
		LocalDateTime now;
		Timestamp ts_now = now.timestamp(); 
		string localTime(DateTimeFormatter::format(now, "%Y_%m_%d_%H_%M_%S_%i"));
		_logger.information("%s", fmt::format("[startLogFileExpired]now:{} MAXIMUM_EVENT_LOG_FILE_VALIDTY_DURATION_S():{}", localTime.c_str(), MAXIMUM_EVENT_LOG_FILE_VALIDTY_DURATION_S()));

		Timestamp ts_time  = (m_logFileCtxs.front()).m_time.timestamp();
		
		int64_t diff = static_cast<int64_t>(ts_now.epochTime()) - ts_time.epochTime();

		int64_t timeToExpired = MAXIMUM_EVENT_LOG_FILE_VALIDTY_DURATION_S() - diff;

		_logger.information("%s", fmt::format("[startLogFileExpired]front file name:{}",(m_logFileCtxs.front()).m_name));
		_logger.information("%s", fmt::format("[startLogFileExpired]ts_now:{}, ts_time:{}, diff:{}, timeToExpired:{}",ts_now.epochTime(), ts_time.epochTime(), diff, timeToExpired));
	
		//<< [LE0-9065] TracyTu 
		if(timeToExpired <= 0)
		{
			timeToExpired = EVENT_LOG_DEL_EXPIRED_NOT_ALLOW;			
		}
		//>> [LE0-9065] 

		if(!isCalledInTimeOutCallBack)
		{
			m_logFileExpiredTimer.setStartInterval(second_to_millisecond(timeToExpired));
			m_logFileExpiredTimer.setPeriodicInterval(second_to_millisecond(timeToExpired)); 
			m_logFileExpiredTimer.start(TimerCallback<CeCallEventLog>(*this, &CeCallEventLog::logFileExpiredTimerCallBack), "ecall_eventlog_expired_timer");
		}
		else
		{
			m_logFileExpiredTimer.restart(second_to_millisecond(timeToExpired));
		}
	}
}

void CeCallEventLog::monitorLogFileExpired()
{
	_logger.notice("monitorLogFileExpired!"); 
	
    m_logFileExpiredTimer.stop();

	delExpiredLogFile();
    
	startLogFileExpired();  
}

CeCallEventLog::sEcall_LogFileCtx CeCallEventLog::generateLogFileCtx(std::string& fileName)
{
	sEcall_LogFileCtx logFileCtx;
	std::string::size_type pos = fileName.find("_eventlog_")	;
	_logger.notice("%s", fmt::format("generateLogFileCtx.fileName:{}", fileName.c_str()));
	if(std::string::npos != pos)
	{
		logFileCtx.m_name = fileName;

		std::string tempStr = fileName.substr(0U, pos);

		_logger.information("%s", fmt::format("generateLogFileCtx.tempStr:{}", tempStr.c_str()));

		int tzd;
		DateTime dt;
		bool ok =  DateTimeParser::tryParse("%Y_%m_%d_%H_%M_%S_%i", tempStr, dt, tzd);
		_logger.information("%s", fmt::format("generateLogFileCtx.parse.result:{}", ok));
		if(ok)
		{
			logFileCtx.m_time = LocalDateTime(tzd,dt);
		}
		else
		{
			const LocalDateTime now;
			logFileCtx.m_time = now;
		}
	}
	else
	{
		logFileCtx.m_name = invalid_str;
	}

	_logger.information("%s", fmt::format("logFileCtx.m_name:{}", logFileCtx.m_name.c_str()));

	string localTime(DateTimeFormatter::format(logFileCtx.m_time, "%Y_%m_%d_%H_%M_%S_%i"));
	_logger.information("%s", fmt::format("logFileCtx.m_time:{}", localTime.c_str()));
	return logFileCtx;
}

void CeCallEventLog::removeLogFileElement(std::string& fileName)
{
	if(auto iter = std::find_if(std::begin(m_logFileCtxs), std::end(m_logFileCtxs), [&](const auto& elem) {
		return elem.m_name == fileName;}); iter != std::end(m_logFileCtxs))
	{
       (void)m_logFileCtxs.erase(iter);
	}
}

void CeCallEventLog::addLogFileElement(std::string& fileName)
{
	sEcall_LogFileCtx logFileCtx = generateLogFileCtx(fileName);
	if(logFileCtx.m_name != invalid_str)
	{
		(void)m_logFileCtxs.emplace_back(logFileCtx);
	}
	else
	{
		_logger.error("%s", fmt::format("addLogFileElement invalid file name:{}", fileName.c_str()));
	}	
}

void CeCallEventLog::buildLogFiles()
{
	_logger.information("CeCallEventLog::buildLogFiles!"); 

	m_folderPath.clear();
	m_folderPath = std::string(MD_DATA_DIR)+"/"+EVENTLOG_FOLDER_PATH; //LE0WNC-9813
   
    Poco::File folder(m_folderPath);
	folder.createDirectories();
	
    DirectoryIterator it(folder);
	const DirectoryIterator end;

    m_logFileCtxs.clear(); 

	while (it != end)
	{
	    Poco::Path p(it->path());
    	std::string filename = p.getFileName();
		_logger.notice("%s", fmt::format("buildLogFiles.filename:{}", filename.c_str()));
		addLogFileElement(filename);
		++it;
	}

	monitorLogFileExpired();
}

void CeCallEventLog::initLogFile()
{
	m_folderPath.clear();
	m_folderPath = std::string(MD_DATA_DIR)+"/"+EVENTLOG_FOLDER_PATH; //LE0WNC-9813
    
    Poco::File folder(m_folderPath);
	folder.createDirectories();
	
    DirectoryIterator it(folder);
	const DirectoryIterator end;

	m_fileNumber++;
	if(m_fileNumber > ECALL_EVENT_FILE_NUMBER)
	{
		m_fileNumber = 1U;		
	}

    _logger.notice("%s", fmt::format("initLogFile.m_fileNumber:{}", m_fileNumber));

	pecallMgr->peCallXmlConfig()->saveEcallEventLogFileNumber(m_fileNumber);

	std::string fileNumber = std::string("_eventlog_");
	(void)fileNumber.append(format("%03u", m_fileNumber)); 

	while (it != end)
	{
    	Poco::Path p(it->path());
    	std::string filename = p.getFileName();
		_logger.information("%s", fmt::format("filename:{}", filename.c_str()));
		const std::size_t found=filename.find(fileNumber);
        if (found!=std::string::npos)
        {
            _logger.information("%s", fmt::format("remove:{}", filename.c_str()));
			Poco::File eventFile(p);
			eventFile.remove();
			removeLogFileElement(filename);
            break;
		}

		++it;
	}

	const LocalDateTime now;
	m_fileName.clear();
	m_fileName = DateTimeFormatter::format(now, "%Y_%m_%d_%H_%M_%S_%i");
	(void)m_fileName.append(fileNumber);
	(void)m_fileName.append(std::string(".json")); 
	
	addLogFileElement(m_fileName);

	monitorLogFileExpired();

	_logger.notice("%s", fmt::format("m_folderPath:{} m_fileName:{}", m_folderPath.c_str(), m_fileName.c_str()));
}

void CeCallEventLog::startSession() {
    m_sessionId++;
    _logger.notice("%s", fmt::format("[startSession] ecall started and m_sessionId:{}", m_sessionId));

    pecallMgr->peCallXmlConfig()->saveEcallEventLogPropid(m_sessionId);
    dynamicEvents.clear();

    // << [LE0-15172] TracyTu, do update cell and gnss data in call session start period
    updateCellData(false);
    updateGnssData();
    // >> [LE0-15172]

    initLogFile();

    initEventObjectsForNewSession();
    createInitialEventLog();
    startPeriodTimer();  // << [LE0-15172] TracyTu, do update cell and gnss data in call session start period >>
    m_SessionState = E_SESSION_STATE_START;
}

void CeCallEventLog::endSession() {
    _logger.notice("[endSession]: ecall is end!!!");
    updateEventLog(m_sEventLog.getEcallSessionLog());
    stopPeriodTimer();  // << [LE0-15172] TracyTu, do update cell and gnss data in call session start period >>
    m_SessionState = E_SESSION_STATE_STOP;
}

void CeCallEventLog::collect_event(Object::Ptr event)
{
   std::stringstream ss_str;
   event->stringify(ss_str, 4U);
   std::string s_str = ss_str.str();

   (void)dynamicEvents.emplace_back(s_str);
}

void CeCallEventLog::update()
{
   _logger.information("[update]"); 

   	Object::Ptr prop = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
	(void)prop->set("id", m_sEventLog.getProp().getId());
	(void)prop->set("usage", m_sEventLog.getProp().getUsage());
	(void)prop->set("url", m_sEventLog.getProp().getUrl());
	(void)prop->set("version", m_sEventLog.getProp().getVersion());

   Array::Ptr event_records = new Array(); 
  
   const Poco::JSON::ParseHandler::Ptr pHandler = new Poco::JSON::ParseHandler(true);
   Poco::JSON::Parser p(pHandler);

   for (uint32_t i = 0U; i < dynamicEvents.size(); i++)
	{
		std::string jsonString = dynamicEvents[i];
		Poco::Dynamic::Var result = p.parse(jsonString);
		const Poco::JSON::Object::Ptr pObject = result.extract<Poco::JSON::Object::Ptr>();
	    (void)event_records->add(pObject);

		_logger.notice("%s", fmt::format("[update]: parse jsonString:{}", jsonString));
	}

	Object::Ptr event = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
	(void)event->set("prop", prop);
	(void)event->set("log", event_records); 
    store(event);
}

void CeCallEventLog::updateEventLog(const sEcall_Logfile_eCall_session& ecallSession)
{   
	if(isSessionStart()) 
	{
		_logger.information("[updateEventLog] sEcall_Logfile_eCall_session"); 	
		Object::Ptr ecall_session_log = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)ecall_session_log->set("time", ecallSession.getTime());
		(void)ecall_session_log->set("type", ecallSession.getType());
		(void)ecall_session_log->set("state", ecallSession.getState());
		(void)ecall_session_log->set("odometer", ecallSession.getOdometer());
		(void)ecall_session_log->set("tcuTemp", ecallSession.getTcuTemp());

		collect_event(ecall_session_log);
		update();		
	}
}

void CeCallEventLog::updateEventLog(const sEcall_Logfile_CANinfo_crash& canCrashInfo)
{
    if(isSessionStart())
	{
		_logger.information("[updateEventLog] _sEcall_Logfile_CANinfo_crash"); 	
		Object::Ptr CANinfo_crash_log = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)CANinfo_crash_log->set("time", canCrashInfo.getTime());
		(void)CANinfo_crash_log->set("type", canCrashInfo.getType());
		(void)CANinfo_crash_log->set("content", canCrashInfo.getContent());

		collect_event(CANinfo_crash_log);
		update();		
	}
}

void CeCallEventLog::updateEventLog(sEcall_Logfile_Cellular_network& cellNetwork)
{
	cellNetwork.setTime(get_unix_time());
	
    if(isSessionStart())
	{
		_logger.information("[updateEventLog] sEcall_Logfile_Cellular_network"); 		
		Object::Ptr Cellular_network_cell = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)Cellular_network_cell->set("id", cellNetwork.getCell().getId());
		(void)Cellular_network_cell->set("name", cellNetwork.getCell().getName());
		(void)Cellular_network_cell->set("mcc", cellNetwork.getCell().getMcc());
		(void)Cellular_network_cell->set("mnc", cellNetwork.getCell().getMnc());
		(void)Cellular_network_cell->set("rat", cellNetwork.getCell().getRat());
		(void)Cellular_network_cell->set("rxlevel", cellNetwork.getCell().getRxlevel());
		
		Object::Ptr Cellular_network_log = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)Cellular_network_log->set("time", cellNetwork.getTime());
		(void)Cellular_network_log->set("type", cellNetwork.getType());
		(void)Cellular_network_log->set("state", cellNetwork.getState());
		(void)Cellular_network_log->set("cell", Cellular_network_cell);

		if(cellNetwork.getCell().getRat() == "GSM")
		{
			Object::Ptr Cellular_network_signal_gsm = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
			(void)Cellular_network_signal_gsm->set("ss", cellNetwork.getSignal().GSM_signal.getSs());
			(void)Cellular_network_signal_gsm->set("ber", cellNetwork.getSignal().GSM_signal.getBer());

			(void)Cellular_network_log->set("signal", Cellular_network_signal_gsm);
		} 
		else if(cellNetwork.getCell().getRat() == "WCDMA")
		{
			Object::Ptr Cellular_network_signal_wcdma = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
			(void)Cellular_network_signal_wcdma->set("ss", cellNetwork.getSignal().UMTS_signal.getSs());
			(void)Cellular_network_signal_wcdma->set("ber", cellNetwork.getSignal().UMTS_signal.getBer());
			(void)Cellular_network_signal_wcdma->set("ecio", cellNetwork.getSignal().UMTS_signal.getEcio());
			(void)Cellular_network_signal_wcdma->set("rscp", cellNetwork.getSignal().UMTS_signal.getRscp());

			(void)Cellular_network_log->set("signal", Cellular_network_signal_wcdma);
		}
		else if(cellNetwork.getCell().getRat() == "LTE")
		{
			Object::Ptr Cellular_network_signal_lte = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
			(void)Cellular_network_signal_lte->set("ss", cellNetwork.getSignal().LTE_signal.getSs());
			(void)Cellular_network_signal_lte->set("rsrq", cellNetwork.getSignal().LTE_signal.getRsrq());
			(void)Cellular_network_signal_lte->set("rsrp", cellNetwork.getSignal().LTE_signal.getRsrp());
			(void)Cellular_network_signal_lte->set("snr", cellNetwork.getSignal().LTE_signal.getSnr());

			(void)Cellular_network_log->set("signal", Cellular_network_signal_lte);
		}
		else
		{
			_logger.information("[updateEventLog] Invaliad network!"); 
		}
		collect_event(Cellular_network_log);
		update();		
	}
}

void CeCallEventLog::updateEventLog(sEcall_Logfile_GNSS_fix& gnss)
{
	if(isSessionStart())
	{
		_logger.information("[updateEventLog] sEcall_Logfile_GNSS_fix"); 	

		Object::Ptr GNSS_fix_dop = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)GNSS_fix_dop->set("p", gnss.getDop().p);
		(void)GNSS_fix_dop->set("h", gnss.getDop().h);
		(void)GNSS_fix_dop->set("v", gnss.getDop().v);
		
		Object::Ptr GNSS_fix_sat = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)GNSS_fix_sat->set("snr", gnss.getSat().snr);

		Object::Ptr GNSS_fix_err = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)GNSS_fix_err->set("hErr", gnss.getErr().hErr);

		Object::Ptr GNSS_fix_log = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)GNSS_fix_log->set("time", gnss.getTime());
		(void)GNSS_fix_log->set("type", gnss.getType());
		(void)GNSS_fix_log->set("fix", gnss.getFix());
		(void)GNSS_fix_log->set("system", gnss.getSystem());
		(void)GNSS_fix_log->set("dop", GNSS_fix_dop);
		(void)GNSS_fix_log->set("sat", GNSS_fix_sat);
		(void)GNSS_fix_log->set("err", GNSS_fix_err);

		collect_event(GNSS_fix_log);
		update(); 
	}
}

void CeCallEventLog::updateEventLog(const sEcall_Logfile_PSAP_call& psapCall)
{
	if(isSessionStart())
	{
		_logger.information("[updateEventLog] sEcall_Logfile_PSAP_call"); 	
		Object::Ptr PSAP_call_log = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)PSAP_call_log->set("time", psapCall.getTime());
		(void)PSAP_call_log->set("type", psapCall.getType());
		(void)PSAP_call_log->set("state", psapCall.getState());

		collect_event(PSAP_call_log);
		update(); 		
	}
}

void CeCallEventLog::updateEventLog(const sEcall_Logfile_MSD_transmission& msdTrans)
{
	if(isSessionStart())
	{
		sEcall_eCall_session_GNSS_init GNSS_init = {};
		getGnssInitData(GNSS_init);

		_logger.information("[updateEventLog] sEcall_Logfile_MSD_transmission"); 	
		Object::Ptr MSD_transmission_gnss_dop = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)MSD_transmission_gnss_dop->set("p", GNSS_init.getDop().p);
		(void)MSD_transmission_gnss_dop->set("h", GNSS_init.getDop().h);
		(void)MSD_transmission_gnss_dop->set("v", GNSS_init.getDop().v);
		
		Object::Ptr MSD_transmission_gnss_sat = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)MSD_transmission_gnss_sat->set("snr", GNSS_init.getSat().snr);

		Object::Ptr MSD_transmission_gnss_err = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)MSD_transmission_gnss_err->set("hErr", GNSS_init.getErr().hErr);

		Object::Ptr MSD_transmission_gnss = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)MSD_transmission_gnss->set("fix", GNSS_init.getFix());
		(void)MSD_transmission_gnss->set("system", GNSS_init.getSystem());
		(void)MSD_transmission_gnss->set("dop", MSD_transmission_gnss_dop);
		(void)MSD_transmission_gnss->set("sat", MSD_transmission_gnss_sat);
		(void)MSD_transmission_gnss->set("err", MSD_transmission_gnss_err);


		Object::Ptr MSD_transmission_log = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)MSD_transmission_log->set("time", msdTrans.getTime());
		(void)MSD_transmission_log->set("type", msdTrans.getType());
		(void)MSD_transmission_log->set("state", msdTrans.getState());
		(void)MSD_transmission_log->set("source", msdTrans.getSource());
		(void)MSD_transmission_log->set("gnss", MSD_transmission_gnss);
		
		collect_event(MSD_transmission_log);
		update();		
	}
}

void CeCallEventLog::updateEventLog(const sEcall_Logfile_Vehicle& vehicle)
{
	if(isSessionStart())
	{
		_logger.information("[updateEventLog] sEcall_Logfile_Vehicle"); 	
		Object::Ptr Vehicle_log = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)Vehicle_log ->set("time", vehicle.getTime());
		(void)Vehicle_log ->set("type", vehicle.getType());
		(void)Vehicle_log ->set("sev", vehicle.getSev());
		(void)Vehicle_log ->set("canState", vehicle.getCanState());

		collect_event(Vehicle_log);
		update(); 
	}
}

void CeCallEventLog::updateEventLog(const sEcall_Logfile_TCU& tcu)
{
	if(isSessionStart())
	{
		_logger.information("[updateEventLog] sEcall_Logfile_TCU"); 	
		Object::Ptr TCU_log = new Object(Poco::JSON_PRESERVE_KEY_ORDER);
		(void)TCU_log->set("time", tcu.getTime());
		(void)TCU_log->set("type", tcu.getType());
		(void)TCU_log->set("power", tcu.getPower());

		collect_event(TCU_log);
		update(); 
	}
}

uint32_t CeCallEventLog::get_vehicle_odometer()
{
	bool ret = false;
	uint32_t result{0U};
	canDcRawFrameData_t cf2{0U, 0x0U, 8U, { 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U } };
	ret = pecallMgr->pDataStore()->getCANFrame( static_cast<uint16_t>(Frame_DONNEES_BSI_LENTES_4), cf2 );
	_logger.information("get_vehicle_odometer ret = %b ", ret);
	if(ret)
	{
        uint32_t odometer = pecallMgr->pDataStore()->parseCANData(cf2.payload, 7, 24);
		_logger.information("%s", fmt::format("odometer:{}",odometer));
		result = odometer;
	}
	return result;
}

std::string CeCallEventLog::get_vehicle_config_mode()
{
	bool ret = false;
    std::string result = pecallMgr->peCallMachine()->getInternalModeConfigVhl(); // << [LE0-10901] 20240223 ck >>
	canDcRawFrameData_t cf2{0U, 0x0U, 8U, { 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U } };
	ret = pecallMgr->pDataStore()->getCANFrame( static_cast<uint16_t>(Frame_DONNEES_BSI_LENTES_2), cf2 );
	_logger.information("get_vehicle_config_mode ret = %b ", ret);
	if(ret)
	{
        uint32_t config_mode = pecallMgr->pDataStore()->parseCANData(cf2.payload, 3, 4);
		_logger.information("%s", fmt::format("config_mode:{}, payload:{}",config_mode, cf2.can_to_str()));
		const auto it = Enum_Vehicle_configMode.find(config_mode);
		if(it != Enum_Vehicle_configMode.end())
		{
           _logger.information("%s", fmt::format("config_mode:{}, name:{}",config_mode, it->second));
		   result = it->second;
           pecallMgr->peCallMachine()->setInternalModeConfigVhl(result); // << [LE0-10901] 20240223 ck >>
           pecallMgr->peCallXmlConfig()->saveModeConfigVhl(result); // << [LE0-10901] 20240223 ck >>
		}
	}
    return result;
}

void CeCallEventLog::get_Identification_Zone_for_Downloadable_ECU_service(DATA_IdentificationZone& data)
{
    (void)pecallMgr->pDataStore()->getIdentificationZone(data);
	_logger.information("%s", fmt::format("get_Identification_Zone_for_Downloadable_ECU_service SW_VER:{} SW_EDITION:{} SW_REFERENCE0:{} SW_REFERENCE1:{} SW_REFERENCE2:{}", data.SW_VER, data.SW_EDITION,data.SW_REFERENCE[0],data.SW_REFERENCE[1],data.SW_REFERENCE[2]));
}

std::string CeCallEventLog::get_product_number()
{
    DATA_PSA_AUTH output;

	(void)pecallMgr->pDataStore()->getPSAAuth(output);
    _logger.information("%s", fmt::format("FPRN:{}", output.FPRN[0]));
	_logger.information("%s", fmt::format("FPRN:{}", output.FPRN[1]));
	_logger.information("%s", fmt::format("FPRN:{}", output.FPRN[2]));
	_logger.information("%s", fmt::format("FPRN:{}", output.FPRN[3]));
	_logger.information("%s", fmt::format("FPRN:{}", output.FPRN[4]));
	std::string productNumber(fmt::format("{:02d}{:02d}{:02d}{:02d}{:02d}", output.FPRN[0], output.FPRN[1], output.FPRN[2], output.FPRN[3], output.FPRN[4]));
	return productNumber;
}

std::string CeCallEventLog::get_regulation_xswids_num()
{
	DATA_RegulationXSwIdsNum data;
	(void)pecallMgr->pDataStore()->getRegulationXSwIdsNum(data);
	_logger.information("%s", fmt::format("get_regulation_xswids_num data:{}", data.to_DID()));
	return data.to_DID();
}

int64_t CeCallEventLog::get_unix_time(){
	int64_t unixTime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	_logger.information("%s", fmt::format("unixTime:{}",unixTime));
	return unixTime;
}

uint8_t CeCallEventLog::get_info_crash() {
	bool ret = false;
	uint8_t result{0U};
	canDcRawFrameData_t cf2{0U, 0x0U, 8U, { 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U } };
	ret = pecallMgr->pDataStore()->getCANFrame( static_cast<uint16_t>(Frame_ETAT_INFO_CRASH), cf2 );
	_logger.information("get_info_crash ret = %b ", ret);
	if(ret)
	{
		uint8_t infoCrash = static_cast<uint8_t>(pecallMgr->pDataStore()->parseCANData(cf2.payload, 8, 1));
		_logger.information("%s", fmt::format("get_info_crash infoCrash:{}",infoCrash));
		result = infoCrash;
	}
	return result;
}

std::string CeCallEventLog::get_International_Mobile_Equipment_Identity()
{
	char str_buf[256] = {0};
	NAD_RIL_Errno ret = nad_device_get_imei(&str_buf[0], 256);

	if(ret == NAD_E_SUCCESS)
	{
		_logger.information("%s", fmt::format("nad_device_get_imei IMEI:{}",str_buf));
	}
	_logger.information("%s", fmt::format("get_International_Mobile_Equipment_Identity ret:{}",ret));   //ERR_WNC_GRPC_CANT_FIND_SPECIFIED_TOKEN
	return std::string(&str_buf[0]);
}

std::string CeCallEventLog::get_Name_of_the_operator()
{
    NAD_RIL_Errno ret = NAD_ERR_GENERIC_FAILURE;
	char op_name[RESP_STR_LENGTH_128] = {0};
    char short_op_name[RESP_STR_LENGTH_64] = {0};
    char plmn[RESP_STR_LENGTH_16] = {0};

	ret = nad_network_get_operator_info(&op_name[0], RESP_STR_LENGTH_128,
                                   &short_op_name[0], RESP_STR_LENGTH_64,
                                   &plmn[0], RESP_STR_LENGTH_16);

	if(ret == NAD_E_SUCCESS) {
		_logger.information("%s", fmt::format("nad_network_get_operator_info op_name:{}", op_name));
	} 
	_logger.information("%s", fmt::format("get_Name_of_the_operator ret:{}",ret));
    
	return std::string(&op_name[0]);
}

NAD_RIL_Errno CeCallEventLog::get_cell_info_list(nad_network_cellinfo_list_t &cell_info_list)
{
	NAD_RIL_Errno ret = NAD_ERR_GENERIC_FAILURE;
	cell_info_list.num = 0;
    ret = nad_network_get_cell_info_list(&cell_info_list);
	_logger.information("%s", fmt::format("get_cell_info_list cell_info_list.num:{}, ret:{}",cell_info_list.num, ret));

	if(ret == NAD_E_SUCCESS)
    {
        for(int i = 0; i < cell_info_list.num; i++)
        {
			_logger.information("%s", fmt::format("rat:{}, registered:{}", cell_info_list.cellsInfo[i].cellInfoType, cell_info_list.cellsInfo[i].registered));
            switch(cell_info_list.cellsInfo[i].cellInfoType)
            {
                case CELL_INFO_TYPE_GSM:
				{
					_logger.information("%s", fmt::format("mcc:{} mnc:{} cid:{} signalStrength:{}", cell_info_list.cellsInfo[i].CellInfo.gsm.cellIdentityGsm.mcc, cell_info_list.cellsInfo[i].CellInfo.gsm.cellIdentityGsm.mnc, cell_info_list.cellsInfo[i].CellInfo.gsm.cellIdentityGsm.cid, cell_info_list.cellsInfo[i].CellInfo.gsm.signalStrengthGsm.signalStrength));
                    break;					
				}

                case CELL_INFO_TYPE_WCDMA:
				{
					_logger.information("%s", fmt::format("mcc:{} mnc:{} cid:{} signalStrength:{}", cell_info_list.cellsInfo[i].CellInfo.wcdma.cellIdentityWcdma.mcc, cell_info_list.cellsInfo[i].CellInfo.wcdma.cellIdentityWcdma.mnc, cell_info_list.cellsInfo[i].CellInfo.wcdma.cellIdentityWcdma.cid, cell_info_list.cellsInfo[i].CellInfo.wcdma.signalStrengthWcdma.signalStrength));
                    break;					
				}

                case CELL_INFO_TYPE_LTE:
				{
	                _logger.information("%s", fmt::format("mcc:{} mnc:{} cid:{} signalStrength:{}", cell_info_list.cellsInfo[i].CellInfo.lte.cellIdentityLte.mcc, cell_info_list.cellsInfo[i].CellInfo.lte.cellIdentityLte.mnc, cell_info_list.cellsInfo[i].CellInfo.lte.cellIdentityLte.ci, cell_info_list.cellsInfo[i].CellInfo.lte.signalStrengthLte.signalStrength));
                    break;					
				}

                default:
                    break;
            }

			if(cell_info_list.cellsInfo[i].registered != 0)
			{
				break;
			}
        }
    }
	return ret;
}

void CeCallEventLog::updateCellData(const bool toBeUpdatedEventLog)
{
	sEcall_Logfile_Cellular_network cell_network;
	
	const NAD_RIL_Errno ret = getCellNetworkCellData(cell_network);
	
	if(ret == NAD_E_SUCCESS)
	{
		sEcall_Logfile_Cellular_network& current_cell_network = m_sEventLog.getCellularNetworkLog();
		_logger.information("%s", fmt::format("updateCellData current_cell_network id:{} cell_network.cell.id:{}", current_cell_network.getCell().getId(), cell_network.getCell().getId()));
		
		if(!current_cell_network.isSameCell(cell_network.getCell()))
		{
			current_cell_network.setCell(cell_network.getCell());
			current_cell_network.setSignal(cell_network.getSignal());

			if(toBeUpdatedEventLog)
			{
				updateEventLog(current_cell_network);				
			}
		} 		
	}
}

void CeCallEventLog::updateGnssData()
{
	sEcall_Logfile_GNSS_fix GNSS_fix;

	getGnssData(GNSS_fix);
	
	sEcall_Logfile_GNSS_fix& current_gnss_fix =  m_sEventLog.getGNSSFixLog();
	_logger.information("%s", fmt::format("updateGnssData current_fix id:{} m_sEventLog.GNSS_fix_log.fix:{}", current_gnss_fix.getFix(), m_sEventLog.getGNSSFixLog().getFix()));

	if(!current_gnss_fix.isSameFix(GNSS_fix.getFix()))
	{
		current_gnss_fix = GNSS_fix;
		updateEventLog(current_gnss_fix);
	} 

}

void CeCallEventLog::TimeOut(Poco::Timer&) {
    _logger.information("updateEventLog TimeOut!");

    // << [LE0-15172] TracyTu, do update cell and gnss data in call session start period
    if (isSessionStart()) {
        updateCellData(true);
        updateGnssData();
    } else {
        _logger.information("skip to update Periodic data due to  session is not started!");
    }
    // >> [LE0-15172]
}

void CeCallEventLog::updateCanInfoCrashContent(const uint32_t param) {
	_logger.notice("[onUpdateEventLog]:CAN_INFO_CRASH_CONTENT");

	m_sEventLog.getCANinfoCrashLog().setTime(get_unix_time());
    m_sEventLog.getCANinfoCrashLog().setType("CAN_INFO_CRASH");
    m_sEventLog.getCANinfoCrashLog().setContent(toString(param, Enum_CANinfo_crash_content, NUM_OF_ELEMENTS(Enum_CANinfo_crash_content)));
    pecallMgr->peCallRemoteServer()->notifyLog(Enum_CANinfo_crash_content[param]);
    updateEventLog(m_sEventLog.getCANinfoCrashLog());
}

void CeCallEventLog::updateCellularNetworkState(const uint32_t errorCode, const uint32_t serviceState) {
	_logger.notice("%s", fmt::format("[onUpdateEventLog]:CELLULAR_NETWORK_STATE with i16ErrorCode:{} eSrvState:{}", errorCode, serviceState));
	if(FIH_NO_ERR == errorCode)
	{
		if(Enum_Registration_state[serviceState] != m_sEventLog.getCellularNetworkLog().getState()){

			m_sEventLog.getCellularNetworkLog().setState(toString(serviceState,Enum_Registration_state, NUM_OF_ELEMENTS(Enum_Registration_state)));
			updateCellData(false);
			updateEventLog(m_sEventLog.getCellularNetworkLog());
		}
	} else {
		_logger.error("[onUpdateEventLog] CELLULAR_NETWORK_STATE: error code: %d \n", errorCode);
	}
}

void CeCallEventLog::updateECallSessionStateStarted(const uint32_t ecallMode, const std::string ecallNumber){
	_logger.information("%s", fmt::format("[onUpdateEventLog]:ECALL_SESSION_STATE ecall_mode:{}", ecallMode));
	m_sEventLog.getEcallSessionLog().getInit().getEcall().setMode(toString(ecallMode,Enum_eCall_session_mode, NUM_OF_ELEMENTS(Enum_eCall_session_mode)));
	m_sEventLog.getEcallSessionLog().getInit().getEcall().setPsapNumber(ecallNumber);

	if(isSessionStart())
    {
		_logger.information("%s", fmt::format("[onUpdateEventLog]:there is unfinished existing call, force to call endSession()!!!!"));
		m_sEventLog.getEcallSessionLog().setState(Enum_eCall_session_state[3]); //STOPPED_NEW_ECALL
		endSession();
	}

	startSession();
}

void CeCallEventLog::updateMsdTransmissionState(const uint32_t param){
	_logger.notice("%s", fmt::format("[onUpdateEventLog]:MSD_TRANSMISSION_STATE param_0:{} m_sEventLog.MSD_transmission_log.state:{}", param, m_sEventLog.getMSDTransmissionLog().getState()));
	if(Enum_MSD_trans_state[param] != m_sEventLog.getMSDTransmissionLog().getState()){

		m_sEventLog.getMSDTransmissionLog().setTime(get_unix_time());
		m_sEventLog.getMSDTransmissionLog().setState(toString(param,Enum_MSD_trans_state, NUM_OF_ELEMENTS(Enum_MSD_trans_state)));
		pecallMgr->peCallRemoteServer()->notifyLog(Enum_MSD_trans_state[param]);
		updateEventLog(m_sEventLog.getMSDTransmissionLog());
	}
}

void CeCallEventLog::updatePSAPCallState(const uint32_t param){
	_logger.notice("%s", fmt::format("[onUpdateEventLog]:PSAP_CALL_STATE param_0:{} m_sEventLog.PSAP_call_log.state:{}", param, m_sEventLog.getPSAPCallLog().getState()));
	if(Enum_PASP_call_state[param] != m_sEventLog.getPSAPCallLog().getState()){

		m_sEventLog.getPSAPCallLog().setTime(get_unix_time());
		m_sEventLog.getPSAPCallLog().setState(toString(param,Enum_PASP_call_state, NUM_OF_ELEMENTS(Enum_PASP_call_state)));
		pecallMgr->peCallRemoteServer()->notifyLog(Enum_PASP_call_state[param]);
		updateEventLog(m_sEventLog.getPSAPCallLog());
	}
}

void CeCallEventLog::updateTCUPower(const uint32_t param){
	_logger.notice("%s", fmt::format("[onUpdateEventLog]:TCU_POWER param_0:{}", param));
	m_sEventLog.getTCULog().setTime(get_unix_time());
	m_sEventLog.getTCULog().setPower(toString(param,Enum_TCU_power, NUM_OF_ELEMENTS(Enum_TCU_power)));
	pecallMgr->peCallRemoteServer()->notifyLog(Enum_TCU_power[param]);
	if((param == static_cast<uint32_t>(TCU_POWER_VEHICLE)) && (!isVehPower()))
	{
		m_TcuPower = TCU_POWER_VEHICLE;
		updateEventLog(m_sEventLog.getTCULog());
	}
	else if((param == static_cast<uint32_t>(TCU_POWER_BACKUP)) && (isVehPower()))
	{
		m_TcuPower = TCU_POWER_BACKUP;
		updateEventLog(m_sEventLog.getTCULog());
	}
	else
	{
		_logger.notice("[updateTCUPower]:Invaliad TCU state!");
	}
}

void CeCallEventLog::updateVEHICLE_SEV(const uint32_t param){
	_logger.notice("%s", fmt::format("[onUpdateEventLog]:VEHICLE_SEV param_0:{} m_sEventLog.Vehicle_log.sev:{}", param, m_sEventLog.getVehicleLog().getSev()));
	if(Enum_Vehicle_sev[param] != m_sEventLog.getVehicleLog().getSev()){

		m_sEventLog.getVehicleLog().setTime(get_unix_time());
		m_sEventLog.getVehicleLog().setSev(toString(param,Enum_Vehicle_sev, NUM_OF_ELEMENTS(Enum_Vehicle_sev)));
		pecallMgr->peCallRemoteServer()->notifyLog(Enum_Vehicle_sev[param]);
		updateEventLog(m_sEventLog.getVehicleLog());
	}
}

void CeCallEventLog::updateVEHICLE_CAN(const uint32_t param){
	_logger.notice("%s", fmt::format("[onUpdateEventLog]:VEHICLE_CAN param_0:{} m_sEventLog.Vehicle_log.canState:{}", param, m_sEventLog.getVehicleLog().getCanState()));
	if(Enum_Vehicle_canState[param] != m_sEventLog.getVehicleLog().getCanState()){
		m_sEventLog.getVehicleLog().setTime(get_unix_time());
		m_sEventLog.getVehicleLog().setCanState(toString(param,Enum_Vehicle_canState, NUM_OF_ELEMENTS(Enum_Vehicle_canState)));
		pecallMgr->peCallRemoteServer()->notifyLog(Enum_Vehicle_canState[param]);
		updateEventLog(m_sEventLog.getVehicleLog());
	}
}

long CeCallEventLog::MAXIMUM_EVENT_LOG_FILE_VALIDTY_DURATION_S()const { 
	return static_cast<long>(pecallMgr->peCallMachine()->getEventLogFileExpiredTime()) * 60; //13 hours /*13*60*60*/
}

// << [LE0-15172] TracyTu, do update cell and gnss data in call session start period
void CeCallEventLog::startPeriodTimer() {
    _logger.notice("startPeriodTimer");
    m_informTimer.setStartInterval(EVENT_LOG_PEIROD_INTERVAL);
    m_informTimer.setPeriodicInterval(EVENT_LOG_PEIROD_INTERVAL);
    m_informTimer.start(TimerCallback<CeCallEventLog>(*this, &CeCallEventLog::TimeOut), "ecall_eventlog_update_timer");
}

void CeCallEventLog::stopPeriodTimer() {
    _logger.notice("stopPeriodTimer");
    m_informTimer.stop();
}
// >> [LE0-15172] 
} } // namespace MD::eCallMgr
