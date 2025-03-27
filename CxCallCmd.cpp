#include "CxCallCmd.h"
#include "Poco/AutoPtr.h"
#include "Poco/Notification.h"
#include "Poco/NotificationQueue.h"
#include "Poco/Format.h"
#include "Poco/NumberParser.h"
#include "xCallManager.h"
#include "CxCallMachine.h"
#include "fmt/core.h"
#include <unistd.h> 
#include <iomanip>
#include "CxCallWSocket.h"
#include "fmt/core.h"
#include "CConfiguration.h"
#include "DataStore.h"
#include "CxCalldebug.h"
#include "CxCallRemoteClient.h"
#include "CxCallHandler.h"
#include "CxCallState.h"
#include "CxCallHttps.h"
#include "CxCallAudio.h"
#include "CxCallLwM2MObserveService.h"
#include "CConfiguration.h"
#include "CMessage.h"
#include "CxCallXmlState.h" // << [LE022-4553] 20241011 EasonCCLiao >>
#include "radiofih/fih_sdk_ril_api.h"
#include "CellularManagerTypes.h"
#include "CxCallHwValidator.h" // << [LE022-1667] TracyTu, Support one image >>
#include "CxCallTrace.h" //<< [LE022-4538] TracyTu, xCall trace develop >>
#include "CxCallXmlConfig.h" //<< [LE022-6913] 20250211 EasonCCLiao >>
#include "../../dlt-daemon/include/dlt/dlt.h" //<< [LE022-4538] TracyTu, xCall trace develop >>
#include <memory>
using Poco::AutoPtr;
using Poco::Notification;
using Poco::NotificationQueue;
using Poco::Thread;
using Poco::NumberParser;

namespace MD::xCallManage {

CxCallCmd::CxCallCmd(BundleContext::Ptr pContext, xCallManager* xCallManager)
    :_logger(CLogger::newInstance("Cmd"))
	,m_pContext(pContext)
	,m_pxCallManager(xCallManager)
    ,mThreadInfo(new CxCallThreadsAliveWatcher::ThreadInfo(&_pThread)) // << [LE0-5619] 20230615 CK >>
{
    CHD.regist(pContext, std::string("xCallManager"),  XCALL_TYPE, &_logger.getLogger());
    init_handle_aCall_cmd_function_map(); 
}
CxCallCmd::~CxCallCmd()
{
  
}
void CxCallCmd::start()
{
	_logger.notice("%s", fmt::format("thread monitor, CxCallCmd::start, tid:{}", _pThread.id())); // << [LE0-3495] 20231006 CK >>
    _pThread.setName("aCall.Cmd"); // << [LE0-5215] 20230530 CK >>
    needstop = false; // << [LE0-5619] 20230614 CK >>
	_pThread.start(*this);	
}

void CxCallCmd::stop()
{
	_logger.notice("%s", fmt::format("thread monitor, CxCallCmd::stop, tid:{}", _pThread.id())); // << [LE0-3495] 20231006 CK >>
	needstop = true;
	_pThread.join();
}
// << [LE0-5619] 20230615 CK
bool CxCallCmd::isLcmSwmNeedAlive() {
    return true;
}
CxCallThreadsAliveWatcher::ThreadInfo* CxCallCmd::getThreadInfo() {
    return mThreadInfo;
}
// >> [LE0-5619]
void CxCallCmd::run()
{
    sendstringcmd(static_cast<uint32_t>(LCM_TYPE), static_cast<uint32_t>(LCM_STATE_QUERY), static_cast<uint16_t>(NORMAL), std::string("GetStatus"), 10); // << [LE0-5724] 20230619 CK: query LCM state >>
    enqueueStringCmd(XCALL_DIAG_STATE_NEED_SYNC, static_cast<uint16_t>(NORMAL), ""); // << [LE0-9412] 20240109 CK >>

    while (!needstop)
    {
        // << [LE0-17233] 20241121 Tracy 
        try{
            AutoPtr<Notification> pNf(CHD.cmd_queue.waitDequeueNotification(200));
            while (pNf)
            {
                Command::Ptr pCmdIn = pNf.cast<Command>();
                poco_assert(pCmdIn.get());

                _logger.rx("%s", fmt::format("[AT] xCallManager get cmdid:{} flag:{} from:{} data:{} queueSize:{}", (long)pCmdIn->getid(), (long)pCmdIn->getflag(), pCmdIn->getFrom(), pCmdIn->getdata(), CHD.cmd_queue.size())); // << [LE022-5019] 20240925 EasonCCLiao >>
                
                switch (pCmdIn->getflag())
                {
                    case NORMAL:
                    {
                        _logger.information("Get Normal command");
                        execCmd(pCmdIn);
                        break;
                    }
                    case REPLY:
                    {
                        _logger.information("Get Reply");
                        execReply(pCmdIn);
                        break;
                    }
                    case TIMEOUT:
                    {
                        _logger.information("Get TIMEOUT command");
                        execTimeout(pCmdIn);
                        break;
                    }
                    default:
                        // default
                        break;
                }

                pNf = CHD.cmd_queue.waitDequeueNotification(200);
            }
        } catch (Poco::Exception &e) {
            _logger.error("%s", fmt::format("Fail to run CxCallCmd, e::{}", e.displayText()));
            m_pxCallManager->pxCallTrace()->setBBSSingleTag(BBS_XCALL_THREAD_EXCEPTION_CATCH, BBS_THREAD_CMD);
            Poco::Thread::sleep(2000);
        } catch (...) {        
            _logger.error("An exception occurred in CxCallCmd::run().");
            m_pxCallManager->pxCallTrace()->setBBSSingleTag(BBS_XCALL_THREAD_EXCEPTION_CATCH, BBS_THREAD_CMD);
            Poco::Thread::sleep(2000);
        } 
        // >> [LE0-17233]      
    }
}
void CxCallCmd::sendstringcmd(uint32_t type, uint32_t id, uint16_t flag, std::string data, unsigned int timeout)
{
    _logger.tx(fmt::format("{} type:{} id:{} flag:{} data:{} timeout:{}",
                            __FUNCTION__,
                            static_cast<int>(type),
                            static_cast<int>(id),
                            static_cast<int>(flag),
                            data,
                            static_cast<int>(timeout)));
    Command::Ptr pCmdOut = new Command(type, id, flag, data);
    poco_check_ptr(pCmdOut.get());
    if(0 != timeout)
    CHD.setForReply(pCmdOut, timeout);
    CHD.sendCmd(pCmdOut);
}
void CxCallCmd::sendvoidcmd(uint32_t type, uint32_t id, uint16_t flag, void* obj, unsigned int timeout)
{
    _logger.tx(fmt::format("{} type:{} id:{} flag:{} timeout:{}",
                            __FUNCTION__,
                            static_cast<int>(type),
                            static_cast<int>(id),
                            static_cast<int>(flag),
                            static_cast<int>(timeout)));
    Command::Ptr pCmdOut = new Command(type, id, flag, obj);
    poco_check_ptr(pCmdOut.get());
    if(0 != timeout)
    CHD.setForReply(pCmdOut, timeout);
    CHD.sendCmd(pCmdOut);
}
void CxCallCmd::sendanycmd(uint32_t type, uint32_t id, uint16_t flag, std::any data, unsigned int timeout)
{
    _logger.tx(fmt::format("{} type:{} id:{} flag:{} timeout:{}",
                            __FUNCTION__,
                            static_cast<int>(type),
                            static_cast<int>(id),
                            static_cast<int>(flag),
                            static_cast<int>(timeout)));
    Command::Ptr pCmdOut = new Command(type, id, flag, data);
    poco_check_ptr(pCmdOut.get());
    if(0 != timeout)
    CHD.setForReply(pCmdOut, timeout);
    CHD.sendCmd(pCmdOut);
}
// << [LE0-9412] 20240109 CK
void CxCallCmd::enqueueStringCmd(uint32_t id, uint16_t flag, std::string data) {
    Command::Ptr pCmdOut = new Command(XCALL_TYPE, id, flag, data);
    pCmdOut->cmd_from = XCALL_TYPE;
    CHD.cmd_queue.enqueueNotification(pCmdOut);
}
void CxCallCmd::enqueueAnyCmd(uint32_t id, uint16_t flag, std::any data) {
    Command::Ptr pCmdOut = new Command(XCALL_TYPE, id, flag, data);
    pCmdOut->cmd_from = XCALL_TYPE;
    CHD.cmd_queue.enqueueNotification(pCmdOut);
}
// >> [LE0-9412]

void CxCallCmd::execCmd(Command::Ptr pCmd)
{
    switch ((pCmd->getid()/1000)*1000 ){
        case XCALL_CMD_BASE:{
            _logger.information("execCmd:XCALL_CMD_BASE");
			exec_xcall_Cmd(pCmd);
            break;
        }
        // << [LE0-9412] 20240109 CK
        case MCU_CMD_BASE:{
            _logger.information("execCmd:MCU_CMD_BASE");
            exec_mcu_Cmd(pCmd);
            break;
        }
        // >> [LE0-9412]
        case LCM_CMD_BASE:{
            _logger.information("execCmd:LCM_CMD_BASE"); 
			if(LCM_TYPE == pCmd->getFrom())
			exec_lcm_Cmd(pCmd);
            break;
        }
		case CELLULAR_CMD_BASE:{
            _logger.information("execCmd:CELLULAR_CMD_BASE"); 
			exec_cellular_Cmd(pCmd);
            break;
        }
		case CONNMANAGER_BASE:{
            _logger.information("execCmd:CONNMANAGER_BASE"); 
			exec_conn_Cmd(pCmd);
            break;
        }
		case AUDIO_CMD_BASE:{
            _logger.information("execCmd:AUDIO_CMD_BASE");
            exec_audio_Cmd(pCmd);
            break;
        }
        // << [LE0WNC-2771] 20230418 ZelosZSCao
        case URC_CMD_BASE:
            _logger.information("execCmd:URC_CMD_BASE");
            exec_urc_Cmd(pCmd);
            break;
        // >> [LE0WNC-2771]
        default:
			_logger.warning("%s", fmt::format("Get a command ID:{} but NO processing!",pCmd->getid()));
            break;
    }
}
void CxCallCmd::exec_audio_Cmd(Command::Ptr pCmd)
{
   switch (pCmd->getid())
   {
       // << [LE022-1190] 20240205 CK
        case AUDIO_WAV_PLAY_STATE:{
            if (XCALL_TYPE == pCmd->getFrom()) {
                _logger.information("exec_audio_Cmd:AUDIO_WAV_PLAY_STATE");
                m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_AUDIO_WAV_PLAY_STATE), pCmd->getdata());
            }
            break;
        }
        // << [LE0WNC-2594] 20230119 CK
        case AUDIO_NEED_SYNC_XXCALL_STATE:{
            _logger.information("exec_audio_Cmd:AUDIO_NEED_SYNC_XXCALL_STATE");
            sendstringcmd(AUDIOMANAGER_TYPE, m_pxCallManager->pxCallMachine()->internal_data.LIFE_CYCLE_STATE, static_cast<uint16_t>(NORMAL), pCmd->getdata()); // << [LE0-5724] 20230617 CK >>
            sendstringcmd(AUDIOMANAGER_TYPE, AUDIO_NOT_SUPPORT_XXCALL_STATE, static_cast<uint16_t>(NORMAL), std::string(""));
            break;
        }
        // >> [LE0WNC-2594]
       // >> [LE022-1190]
        default:
			_logger.warning("%s", fmt::format("Get a command ID:{} but NO processing!",pCmd->getid()));
            break;
    }
}
void CxCallCmd::exec_conn_Cmd(Command::Ptr pCmd)
{
   switch (pCmd->getid())
   {
        case CONNMANAGER_DATA_PATH_NOTIFY:{
            _logger.information("exec_conn_Cmd:CONNMANAGER_DATA_PATH_NOTIFY");
			int* connection = (int*) pCmd->getObj();
            int Wifi = 1;
            int Cellular = 2;
            if(connection != NULL)
            {
                // << [LE0WNC-3064] 20230213 CK
                _logger.notice("%s", fmt::format("exec_conn_Cmd:CONNMANAGER_DATA_PATH_NOTIFY connection data path:{}", *connection));
                if(*connection == Wifi || *connection == Cellular)
                //if(*connection == Wifi)
                // >> [LE0WNC-3064]
                {
                    _logger.information("Get Connection success");
                    m_pxCallManager->pMessage()->m_data_channels = true;
                    m_pxCallManager->pxCallRemoteClient()->setRemoteCommand(pCmd->getid(), std::string("1"));  //<< [LE0-11230]TracyTu, TPS ecall requirement >>
                }
                else
                {
                    _logger.information("Get Connection fail");
                    m_pxCallManager->pMessage()->m_data_channels = false;
                    m_pxCallManager->pxCallRemoteClient()->setRemoteCommand(pCmd->getid(), std::string("0"));  //<< [LE0-11230]TracyTu, TPS ecall requirement >>
                }
            }
			break;
        }
        default:
			_logger.warning("%s", fmt::format("Get a command ID:{} but NO processing!",pCmd->getid()));
            break;
    }
}

void CxCallCmd::exec_cellular_Cmd(Command::Ptr pCmd)
{
   switch (pCmd->getid())
   {
        case CELLULAR_VOICE_GET_CALL_LIST_REQ:{
            _logger.information("exec_cellular_Cmd:CELLULAR_VOICE_GET_CALL_LIST_REQ");
			m_pxCallManager->pxCallHandler()->putAnyToStateQ(BCALL_CELLULAR_GET_CALL_LIST_REQ, pCmd->getAnydata());
			break;
        }
		case CELLULAR_SIM_GET_IMSI_REQ:{
            _logger.information("exec_cellular_Cmd:CELLULAR_SIM_GET_IMSI_REQ");
            m_pxCallManager->pMessage()->m_imsi = pCmd->getdata();
            //<< [LE0-11230]TracyTu, TPS ecall requirement 
            _logger.notice("%s", fmt::format("CELLULAR_SIM_GET_IMSI_REQ from:{}, imsi string:{}", pCmd->getFrom(), pCmd->getdata()));
            m_pxCallManager->pxCallRemoteClient()->setRemoteCommand(pCmd->getid(), pCmd->getdata()); 
            //>> [LE0-11230]
			break;
        }
        // << [LE0WNC-10142] 20230425 CK
        case CELL_ACALL_SMS_NUM: {
            _logger.information("exec_cellular_Cmd:CELL_ACALL_SMS_NUM");
            _logger.notice("%s", fmt::format("CELL_ACALL_SMS_NUM from:{}, BCALL_SMS_NUMBER:{}", pCmd->getFrom(), pCmd->getdata()));
            // << [LE0-18383] 20250207 CK
            std::string smsNumber = pCmd->getdata();
            if (m_pxCallManager->pxCallMachine()->isValidSMSNumber(smsNumber)) {
                m_pxCallManager->pxCallMachine()->config.BCALL_SMS_NUMBER = smsNumber;
                m_pxCallManager->pxCallXmlConfig()->saveAcallSmsNumber(smsNumber);
            } else {
                m_pxCallManager->pxCallTrace()->setBBSReceivedInvalidSMSNumber(invalidSmsNumberReceivedFrom::receivedFromCellular);
            }
            // >> [LE0-18383]
            break;
        }
        // >> [LE0WNC-10142]
        // << [LE0WNC-1952] 20230512 CK
        case CELL_ECALL_SMS_NUM:
            //<< [LE0-11230]TracyTu, TPS ecall requirement 
            _logger.notice("%s", fmt::format("CELL_ECALL_SMS_NUM from:{}, ECALL_SMS_NUMBER:{}", pCmd->getFrom(), pCmd->getdata()));
            m_pxCallManager->pxCallRemoteClient()->setRemoteCommand(pCmd->getid(), pCmd->getdata()); 
            //>> [LE0-11230]
            break;
        case CELLULAR_VOICE_SET_VOLTE:
            _logger.warning("%s", fmt::format("Get a command CELLULAR_VOICE_SET_VOLTE data:{} but NO processing!", pCmd->getdata()));
            break;
        // >> [LE0WNC-1952]
        default:
			_logger.warning("%s", fmt::format("Get a command ID:{} but NO processing!",pCmd->getid()));
            break;
    }
}

// << [LE0WNC-2771] 20230418 ZelosZSCao
void CxCallCmd::exec_urc_Cmd(Command::Ptr pCmd)
{
    switch (pCmd->getid())
    {
        case CELLULAR_VOICE_URC_DEMOAPP_STATUS_IND:
            _logger.notice("exec_urc_Cmd:CELLULAR_VOICE_URC_DEMOAPP_STATUS_IND");
            m_pxCallManager->pxCallHandler()->putObjToStateQ(BCALL_URC_DEMOAPP_STATUS_IND, pCmd->getObj());
            break;
        default:
            _logger.warning("%s", fmt::format("Get a command ID:{} but NO processing!", pCmd->getid()));
            break;
    }
}
// >> [LE0WNC-2771]

void CxCallCmd::exec_lcm_Cmd(Command::Ptr pCmd)
{
   switch (pCmd->getid())
   {
        case LCM_PARKMODE_STATE_CHANGED:{
			_logger.information("%s", fmt::format("exec_lcm_Cmd:LCM_PARKMODE_STATE_CHANGED:{} {}",pCmd->getFrom(),pCmd->getdata()));
            if(m_pxCallManager->pxCallMachine()->internal_data.PARK_MODE.compare(pCmd->getdata()))
            {
                 m_pxCallManager->pxCallMachine()->internal_data.PARK_MODE = pCmd->getdata();
                m_pxCallManager->pxCallXmlState()->saveParkMode(pCmd->getdata()); // << [LE0-6378][LE0-6366] 20230718 CK >>
                _logger.information("%s", fmt::format("park_mode:{} ",m_pxCallManager->pxCallMachine()->internal_data.PARK_MODE.c_str()));
    			m_pxCallManager->pxCallRemoteClient()->setRemoteCommand(pCmd->getid(), pCmd->getdata());
            }
			sendstringcmd(pCmd->getFrom(), pCmd->getid(), REPLY, std::string("0"));
            m_pxCallManager->pxCallHandler()->xcall_check_crash_recovery(); // << [LE0WNC-2444] 20230103 ZelosZSCao >>
			break;
        }
		case LCM_STATE_INACTIVE:{
			_logger.information("%s", fmt::format("exec_lcm_Cmd:LCM_STATE_INACTIVE:{} {}",pCmd->getFrom(),pCmd->getdata()));
			m_pxCallManager->pxCallHandler()->putAnyToStateQ(BCALL_LCM_STATE_MACHINE, pCmd->getid());
		    sendstringcmd(pCmd->getFrom(), pCmd->getid(), REPLY, std::string("0"));
			break;
        }
		case LCM_STATE_SERVICE_WATCHER:{
			_logger.information("%s", fmt::format("exec_lcm_Cmd:LCM_STATE_SERVICE_WATCHER:{} {}",pCmd->getFrom(),pCmd->getdata()));
			m_pxCallManager->pxCallHandler()->putAnyToStateQ(BCALL_LCM_STATE_MACHINE, pCmd->getid());
		    sendstringcmd(pCmd->getFrom(), pCmd->getid(), REPLY, std::string("0"));
			break;
        }
		case LCM_STATE_NOMINAL:{
			_logger.information("%s", fmt::format("exec_lcm_Cmd:LCM_STATE_NOMINAL:{} {}",pCmd->getFrom(),pCmd->getdata()));
			m_pxCallManager->pxCallHandler()->putAnyToStateQ(BCALL_LCM_STATE_MACHINE, pCmd->getid());
    		sendstringcmd(pCmd->getFrom(), pCmd->getid(), REPLY, std::string("0"));
			break;
        }
		case LCM_STATE_EMERGENCY:{
			_logger.information("%s", fmt::format("exec_lcm_Cmd:LCM_STATE_EMERGENCY:{} {}",pCmd->getFrom(),pCmd->getdata()));
			m_pxCallManager->pxCallHandler()->putAnyToStateQ(BCALL_LCM_STATE_MACHINE, pCmd->getid());
		    sendstringcmd(pCmd->getFrom(), pCmd->getid(), REPLY, std::string("0"));
			break;
        }
		case LCM_STATE_BEFORE_SLEEP:{
			_logger.information("%s", fmt::format("exec_lcm_Cmd:LCM_STATE_BEFORE_SLEEP:{} {}",pCmd->getFrom(),pCmd->getdata()));
			m_pxCallManager->pxCallHandler()->putAnyToStateQ(BCALL_LCM_STATE_MACHINE, pCmd->getid());
		    sendstringcmd(pCmd->getFrom(), pCmd->getid(), REPLY, std::string("0"));
			break;
        }
		case LCM_STATE_UPDATE:{
		    _logger.information("%s", fmt::format("exec_lcm_Cmd:LCM_STATE_UPDATE:{} {}",pCmd->getFrom(),pCmd->getdata()));
            m_pxCallManager->pxCallHandler()->putAnyToStateQ(BCALL_LCM_STATE_MACHINE, pCmd->getid());
			sendstringcmd(pCmd->getFrom(), pCmd->getid(), REPLY, std::string("0"));
			break;
        }
		case LCM_STATE_LOW_POWER_CALLBACK:{
			_logger.information("%s", fmt::format("exec_lcm_Cmd:LCM_STATE_LOW_POWER_CALLBACK:{} {}",pCmd->getFrom(),pCmd->getdata()));
			m_pxCallManager->pxCallHandler()->putAnyToStateQ(BCALL_LCM_STATE_MACHINE, pCmd->getid());
		    sendstringcmd(pCmd->getFrom(), pCmd->getid(), REPLY, std::string("0"));
			break;
        }
		case LCM_THERMAL_MITIGATION:{
		    _logger.information("%s", fmt::format("exec_lcm_Cmd:LCM_THERMAL_MITIGATION:{} {}",pCmd->getFrom(),pCmd->getdata()));
			if(m_pxCallManager->pxCallMachine()->internal_data.THERMAL_MITIGATION.compare(pCmd->getdata()))
			{
                m_pxCallManager->pxCallMachine()->internal_data.setThermalMitigation(pCmd->getdata()); // << [LE0-10901] 20240223 ck >>
                m_pxCallManager->pxCallXmlState()->saveThermalMitigation(pCmd->getdata()); // << [LE0-10901] 20240223 ck >>
				m_pxCallManager->pxCallRemoteClient()->setRemoteCommand(pCmd->getid(), pCmd->getdata());
			}
		    sendstringcmd(pCmd->getFrom(), pCmd->getid(), REPLY, std::string("0"));
			break;
        }
        default:
			_logger.warning("%s", fmt::format("exec_lcm_Cmd:Get a command ID:{} but NO processing!",pCmd->getid()));
            break;
    }

    sendstringcmd(AUDIOMANAGER_TYPE, pCmd->getid(), static_cast<uint16_t>(NORMAL), pCmd->getdata()); // << [LE0-5724] 20230617 CK: Bypass LCM state to audio manager >>
}
// << [LE0-9412] 20240109 CK
void CxCallCmd::exec_mcu_Cmd(Command::Ptr pCmd) {
    switch (pCmd->getid()) {
        case MCU_REQ_SYNC_DIAG_STATE: {
            _logger.information("exec_mcu_Cmd:MCU_REQ_SYNC_DIAG_STATE");
            syncDiagXCallStateStopAndWaitArq();
            break;
        }
        default:
            _logger.warning("%s", fmt::format("Get a command ID:{} but NO processing!", pCmd->getid()));
            break;
    }
}
// >> [LE0-9412]
void CxCallCmd::init_handle_aCall_cmd_function_map() {
    handle_acall_cmd_function_map[XCALL_BCALL_BTN_PRESS] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BCALL_BTN_PRESS();
    };
    handle_acall_cmd_function_map[XCALL_BCALL_BTN_RELEASE] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BCALL_BTN_RELEASE();
    };
    handle_acall_cmd_function_map[XCALL_EM_DEBUG_STATE] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_EM_DEBUG_STATE(pCmd);
    };
    handle_acall_cmd_function_map[XCALL_EM_AWAKENING_NOTIFY] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_EM_AWAKENING_NOTIFY(pCmd);
    };
    handle_acall_cmd_function_map[XCALL_BCALL_MANUAL_TRIGGER] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BCALL_MANUAL_TRIGGER();
    };
    handle_acall_cmd_function_map[XCALL_BCALL_BTN_LONG_PRESS] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BCALL_BTN_LONG_PRESS();
    };
    handle_acall_cmd_function_map[XCALL_BCALL_AUTO_TRIGGER] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BCALL_AUTO_TRIGGER();
    };
    handle_acall_cmd_function_map[XCALL_BCALL_CALL_BACK] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BCALL_CALL_BACK();
    };
    handle_acall_cmd_function_map[XCALL_BCALL_MANUAL_CANCEL] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BCALL_MANUAL_CANCEL();
    };
    handle_acall_cmd_function_map[XCALL_BCALL_CALLOUT_SUCCESS] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BCALL_CALLOUT_SUCCESS();
    };
    handle_acall_cmd_function_map[XCALL_BCALL_CALLOUT_FAIL] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BCALL_CALLOUT_FAIL();
    };
    handle_acall_cmd_function_map[XCALL_BCALL_REGISTRATION_SUCCESS] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BCALL_REGISTRATION_SUCCESS(pCmd);
    };
    handle_acall_cmd_function_map[XCALL_BCALL_MSD_TRANSFER_SUCCESS] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BCALL_MSD_TRANSFER_SUCCESS();
    };
    handle_acall_cmd_function_map[XCALL_BCALL_MSD_REQUEST] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BCALL_MSD_REQUEST(pCmd);
    };
    handle_acall_cmd_function_map[XCALL_BCALL_BTN_SHORT_PRESS] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BCALL_BTN_SHORT_PRESS();
    };
    handle_acall_cmd_function_map[XCALL_STATE_ECALL_WAITING_FOR_CALLBACK] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_STATE_ECALL_WAITING_FOR_CALLBACK();
    };
    handle_acall_cmd_function_map[XCALL_STATE_ECALL_VOICE_COMMUNICATION] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_STATE_ECALL_VOICE_COMMUNICATION();
    };
    handle_acall_cmd_function_map[XCALL_STATE_ECALL_TRIGGERED] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_STATE_ECALL_TRIGGERED();
    };
    handle_acall_cmd_function_map[XCALL_STATE_ECALL_OFF] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_STATE_ECALL_OFF();
    };
    handle_acall_cmd_function_map[XCALL_ECALL_ROUTINE_CTL_REQ] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_ECALL_ROUTINE_CTL_REQ(pCmd);
    };
    handle_acall_cmd_function_map[XCALL_ECALL_ROUTINE_CTL_ABORT] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_ECALL_ROUTINE_CTL_ABORT(pCmd); // << [LE0-7867][LE0-7868][LE0-7863] 20230906 CK >>
    };
    handle_acall_cmd_function_map[XCALL_BACKUP_BATT_NOTIFY] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BACKUP_BATT_NOTIFY(pCmd);
    };
    handle_acall_cmd_function_map[XCALL_STATE_BCALL_CALLBACK_TIMEOUT] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_STATE_BCALL_CALLBACK_TIMEOUT();
    };
    handle_acall_cmd_function_map[XCALL_STATE_ECALL_CALLBACK_TIMEOUT] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_STATE_ECALL_CALLBACK_TIMEOUT(pCmd);
    };
    handle_acall_cmd_function_map[XCALL_BCALL_DM_ACALL_SMS_NUMBER] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BCALL_DM_ACALL_SMS_NUMBER(pCmd);
    };
    handle_acall_cmd_function_map[XCALL_ECALL_BTN_PRESS] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_ECALL_BTN_event(pCmd);
    };
    handle_acall_cmd_function_map[XCALL_ECALL_BTN_RELEASE] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_ECALL_BTN_event(pCmd);
    };
    handle_acall_cmd_function_map[XCALL_ECALL_BTN_SHORT_PRESS] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_ECALL_BTN_event(pCmd);
    };
    handle_acall_cmd_function_map[XCALL_ECALL_BTN_LONG_PRESS] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_ECALL_BTN_event(pCmd);
    };
    handle_acall_cmd_function_map[XCALL_NEED_SYNC_BCALL_TO_ECALL] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_NEED_SYNC_BCALL_TO_ECALL();
    };
    // << [LE0-9412] 20240109 CK
    handle_acall_cmd_function_map[XCALL_DIAG_STATE_UPDATE] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_DIAG_STATE_UPDATE(pCmd);
    };
    handle_acall_cmd_function_map[XCALL_DIAG_STATE_NEED_SYNC] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_DIAG_STATE_NEED_SYNC(pCmd);
    };
    // >> [LE0-9412]

    //<< [LE0-11230]TracyTu, TPS ecall requirement 
    handle_acall_cmd_function_map[XCALL_TPSECALL_SMS_REQUEST] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_TPSECALL_SMS_REQUEST(pCmd);
    };
    //>> [LE0-11230]
    // << [LE0-11009][LE0-11039][LE022-983] 20240301 CK
    // << [LE022-4737] 20240906 EasonCCLiao
    // handle_acall_cmd_function_map[XCALL_DTC_CHANGE_NOTIFY] = [this](Command::Ptr pCmd) {
    //     handle_aCall_cmd_XCALL_DTC_CHANGE_NOTIFY(pCmd);
    // };
    // >> [LE022-4737]
    // >> [LE0-11009][LE0-11039][LE022-983]
    handle_acall_cmd_function_map[XCALL_BCALL_HTTP_MESSAGE_RESPONSE] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BCALL_HTTP_MESSAGE_RESPONSE(pCmd);
    };
    // << [LE0-12307] 20240402 EasonCCLiao
    handle_acall_cmd_function_map[XCALL_ECALL_BTN_BLOCKED] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_ECALL_BTN_BLOCKED(pCmd);
    };
    handle_acall_cmd_function_map[XCALL_BCALL_BTN_BLOCKED] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BCALL_BTN_BLOCKED(pCmd);
    };
    // >> [LE0-12307]
	
    // << [LE022-1667] TracyTu, Support one image
    handle_acall_cmd_function_map[XCALL_DID_CHANGE_NOTIFY] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_DID_CHANGE_NOTIFY(pCmd);
    };
    // >> [LE022-1667] 

    // << [LE0-12560] TracyTu, Push sending to change temporarily TPS eCall parameters
    handle_acall_cmd_function_map[XCALL_TPSECALL_DM_ECALL_SMS_NUMBER] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_TPSECALL_DM_ECALL_SMS_NUMBER(pCmd);
    };
    // >> [LE0-12560]
    // << [LE0-15085] JasonKHLee, XCALL DRIVING STATE UPDATE
    handle_acall_cmd_function_map[XCALL_DRIVING_DIAG_SERVICE_UPDATE] = [this](Command::Ptr pCmd){
        handle_aCall_cmd_XCALL_DRIVING_DIAG_STATE_UPDATE(pCmd);
    };
    handle_acall_cmd_function_map[XCALL_DRIVING_DIAG_STATE_NEED_SYNC] = [this](Command::Ptr pCmd){
        handle_aCall_cmd_XCALL_DRIVING_DIAG_STATE_NEED_SYNC(pCmd);
    };
    // >> [LE0-15085]
    // << [LE022-2567] 20240515 CK
    handle_acall_cmd_function_map[XCALL_ACALL_ECALL_CONNECTED] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_ACALL_ECALL_CONNECTED(pCmd);
    };
    handle_acall_cmd_function_map[XCALL_ACALL_ECALL_DISCONNECTED] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_ACALL_ECALL_DISCONNECTED(pCmd);
    };
    // >> [LE022-2567]
    // << [LE022-2739] 20240528 CK
    handle_acall_cmd_function_map[XCALL_BCALL_RECOVERY_CALL_CRASH] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BCALL_RECOVERY_CALL_CRASH(pCmd);
    };
    handle_acall_cmd_function_map[XCALL_BCALL_RECOVERY_PRESS_CRASH] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_BCALL_RECOVERY_PRESS_CRASH(pCmd);
    };
    // >> [LE022-2739]
    // << [LE022-3863] 20240730 ZelosZSCao
    handle_acall_cmd_function_map[XCALL_ECALL_URC_DEMOAPP_STATUS_IND] = [this](Command::Ptr pCmd) {
        handle_aCall_cmd_XCALL_ECALL_URC_DEMOAPP_STATUS_IND(pCmd);
    };
    // >> [LE022-3863]
}

void CxCallCmd::handle_aCall_cmd_XCALL_DRIVING_DIAG_STATE_NEED_SYNC(Command::Ptr pCmd) {
    _logger.information("exec_ecall_Cmd:handle_aCall_cmd_XCALL_DRIVING_DIAG_STATE_NEED_SYNC");
    syncDiagXCallDrivingStopAndWaitArq();
}

void CxCallCmd::exec_xcall_Cmd(Command::Ptr pCmd)
{
    // << [LE022-4737] 20240906 EasonCCLiao
    // << [LE022-1667] TracyTu, Support one image
    if ((pCmd->getid() != XCALL_DTC_CHANGE_NOTIFY) && (!checkNeedCoverNEENotReadySituation(pCmd)) && can_be_exec(pCmd->getid())) { // << [LE022-2567] 20240515 CK >>
        auto iter = handle_acall_cmd_function_map.find(pCmd->getid());
        if (iter != handle_acall_cmd_function_map.end()) {
            const execFunction func = iter->second;
            func(pCmd);
        }else{
            _logger.warning("%s", fmt::format("Get a command ID:{} but NO processing!",pCmd->getid()));
        }        
    }
    // >> [LE022-1667] 
    // >> [LE022-4737]
}
bool CxCallCmd::exec_dm_reply(Command::Ptr pCmd)
{
    bool isReplayHandleFinished = true; // << [LE0-9412] 20240109 CK >>
   switch (pCmd->getid())
   {
        case DM_OBJECT_GET_OBJECT_INSTANCE:{
            _logger.information("exec_lcm_Cmd:DM_OBJECT_GET_OBJECT_INSTANCE");
			m_pxCallManager->pConfiguration()->analysis_call_config(pCmd->getdata());
			break;
        }
        default:
			_logger.warning("%s", fmt::format("Get a command ID:{} but NO processing!",pCmd->getid()));
            break;
    }
    return isReplayHandleFinished; // << [LE0-9412] 20240109 CK >>
}
bool CxCallCmd::exec_conn_reply(Command::Ptr pCmd)
{
    bool isReplayHandleFinished = true; // << [LE0-9412] 20240109 CK >>
   switch (pCmd->getid())
   {
        case CONNMANAGER_DATA_PATH_QUERY:{
            _logger.information("exec_conn_reply:CONNMANAGER_DATA_PATH_QUERY");
			std::string cmd_data = pCmd->getdata();
            string Wifi = "1";
            string Cellular = "2";
            // << [LE0WNC-3064] 20230213 CK
            _logger.notice("CxCallHttps, exec_conn_reply receive reply of connection data path: %s", cmd_data);
            //Check Connection Status
            if(cmd_data.compare(Wifi)== 0 || cmd_data.compare(Cellular)== 0)
            //if(cmd_data.compare(Wifi)== 0)
            // >> [LE0WNC-3064]
            {
                _logger.information("Get Connection success");
                m_pxCallManager->pMessage()->m_data_channels = true;
                m_pxCallManager->pxCallRemoteClient()->setRemoteCommand(pCmd->getid(), std::string("1"));  //<< [LE0-11230]TracyTu, TPS ecall requirement >>
            }
            else
            {
                _logger.information("Get Connection fail");
                m_pxCallManager->pMessage()->m_data_channels = false;
                m_pxCallManager->pxCallRemoteClient()->setRemoteCommand(pCmd->getid(), std::string("0"));  //<< [LE0-11230]TracyTu, TPS ecall requirement >>
            }
			break;
        }
        default:
			_logger.warning("%s", fmt::format("Get a command ID:{} but NO processing!",pCmd->getid()));
            break;
    }
    return isReplayHandleFinished; // << [LE0-9412] 20240109 CK >>
}
bool CxCallCmd::exec_audio_reply(Command::Ptr pCmd)
{
    bool isReplayHandleFinished = true; // << [LE0-9412] 20240109 CK >>
   switch (pCmd->getid())
   {
        case AUDIO_WAV_START_PLAY:{
            _logger.information("exec_audio_reply:AUDIO_WAV_START_PLAY");
			m_pxCallManager->pxCallHandler()->putAnyToStateQ(BCALL_AUDIO_WAV_START_PLAY, pCmd->getAnydata());
			break;
        }
        default:
			_logger.warning("%s", fmt::format("Get a command ID:{} but NO processing!",pCmd->getid()));
            break;
    }
    return isReplayHandleFinished; // << [LE0-9412] 20240109 CK >>
}
// << [LE0-5724] 20230619 CK
bool CxCallCmd::exec_lcm_reply(Command::Ptr pCmd) {
    bool isReplayHandleFinished = true; // << [LE0-9412] 20240109 CK >>
    switch (pCmd->getid()) {
        case LCM_STATE_QUERY: {
            _logger.notice("%s", fmt::format("exec_lcm_reply:LCM_STATE_QUERY:{} {}", pCmd->getFrom(), pCmd->getdata()));
            std::string cmd_data = pCmd->getdata();

            std::string inactive = "0";
            std::string serviceWatcher = "1";
            std::string nominal = "2";
            std::string emergency = "3";
            std::string beforeSleep = "4";
            std::string update = "5";
            std::string lpc = "6";

            uint32_t cmdId = LCM_STATE_QUERY;
            if (cmd_data.compare(inactive) == 0) {
                cmdId = LCM_STATE_INACTIVE;
            } else if (cmd_data.compare(serviceWatcher) == 0) {
                cmdId = LCM_STATE_SERVICE_WATCHER;
            } else if (cmd_data.compare(nominal) == 0) {
                cmdId = LCM_STATE_NOMINAL;
            } else if (cmd_data.compare(emergency) == 0) {
                cmdId = LCM_STATE_EMERGENCY;
            } else if (cmd_data.compare(beforeSleep) == 0) {
                cmdId = LCM_STATE_BEFORE_SLEEP;
            } else if (cmd_data.compare(update) == 0) {
                cmdId = LCM_STATE_UPDATE;
            } else if (cmd_data.compare(lpc) == 0) {
                cmdId = LCM_STATE_LOW_POWER_CALLBACK;
            }
            Command::Ptr cmd1 = new Command(XCALL_TYPE, cmdId, static_cast<uint16_t>(NORMAL), (string) "");
            exec_lcm_Cmd(cmd1);
            break;
        }
        default:
            _logger.warning("%s", fmt::format("exec_lcm_reply:Get a command ID:{} but NO processing!", pCmd->getid()));
            break;
    }
    return isReplayHandleFinished; // << [LE0-9412] 20240109 CK >>
}
// >> [LE0-5724]
// << [LE0-15085] 20240719 JasonKHLee
bool CxCallCmd::exec_mcu_reply(Command::Ptr pCmd) {
    bool isReplayHandleFinished = true;
    switch (pCmd->getid()) {
        case SOC_SERVICE_SET_DIAG_STATE: {
            isReplayHandleFinished = receiveAckDiagXCallDrivingStopAndWaitArq(pCmd);
            break;
        }
        default:
            _logger.warning("%s", fmt::format("exec_mcu_reply:Get a command ID:{} but NO processing!", pCmd->getid()));
            break;
    }
    return isReplayHandleFinished;
}
// >> [LE0-15085]
// << [LE0-9412] 20240109 CK
bool CxCallCmd::exec_xcall_reply(Command::Ptr pCmd) {
    bool isReplayHandleFinished = true;
    switch (pCmd->getid()) {
        case XCALL_SET_DIAG_STATE: {
            isReplayHandleFinished = receiveAckDiagXCallStateStopAndWaitArq(pCmd);
            break;
        }
        default:
            _logger.warning("%s", fmt::format("exec_xcall_reply:Get a command ID:{} but NO processing!", pCmd->getid()));
            break;
    }
    return isReplayHandleFinished;
}
// >> [LE0-9412]
void CxCallCmd::execReply(Command::Ptr pCmd)
{
	Command::Ptr p_original_cmd = nullptr;
	if(CHD.checkCmdFromWaitQ(pCmd, p_original_cmd))
	{
        bool isReplayHandleFinished = true; // << [LE0-9412] 20240109 CK >>
		// IDs for command you sent before.
		switch ((pCmd->getid()/1000)*1000 )
		{
            // << [LE0-15085] 20240719 JasonKHLee
            case MCU_CMD_BASE:{
                _logger.information("execReply:MCU_CMD_BASE");
                isReplayHandleFinished = exec_mcu_reply(pCmd);
                break;
            }
            // >> [LE0-15085]
            // << [LE0-9412] 20240109 CK
            case XCALL_CMD_BASE:{
                _logger.information("execReply:XCALL_CMD_BASE");
                isReplayHandleFinished = exec_xcall_reply(pCmd);
                break;
            }
            // >> [LE0-9412]
            case DM_CMD_BASE:{
                _logger.information("execReply:DM_CMD_BASE");
                isReplayHandleFinished = exec_dm_reply(pCmd); // << [LE0-9412] 20240109 CK >>
                break;
            }
			case CONNMANAGER_BASE:{
                _logger.information("execReply:CONNMANAGER_BASE");
                isReplayHandleFinished = exec_conn_reply(pCmd); // << [LE0-9412] 20240109 CK >>
                break;
            }
			case AUDIO_CMD_BASE:{
                _logger.information("execReply:AUDIO_CMD_BASE");
                isReplayHandleFinished = exec_audio_reply(pCmd); // << [LE0-9412] 20240109 CK >>
                break;
            }
            case LCM_CMD_BASE:{
                _logger.information("execReply:LCM_CMD_BASE");
                if (LCM_TYPE == pCmd->getFrom()) {
                    isReplayHandleFinished = exec_lcm_reply(pCmd); // << [LE0-9412] 20240109 CK >>
                }
                break;
            }            
			default:
				_logger.warning("Get a reply ID but NO processing!");
				break;
		}
        // << [LE0-9412] 20240109 CK >>
        if (isReplayHandleFinished) {
            CHD.clearForReply(pCmd);
        }
        // >> [LE0-9412]
	}
	else
	{
		_logger.warning("Receive a reply not in wait queue!");
	}
}
void CxCallCmd::exec_mcu_timeout(Command::Ptr pCmd) {
    switch (pCmd->getid()) {
        // << [LE0-15085] 20240719 JasonKHLee >>
        case SOC_SERVICE_SET_DIAG_STATE: {
            timeoutDiagXCallDrivingStopAndWaitArq();
            break;
        }
        // >> [LE0-15085]
        default:
            _logger.warning("%s", fmt::format("exec_xcall_timeout:Get a command ID:{} but NO processing!", pCmd->getid()));
            break;
    }
}
// << [LE0-9412] 20240109 CK
void CxCallCmd::exec_xcall_timeout(Command::Ptr pCmd) {
    switch (pCmd->getid()) {
        case XCALL_SET_DIAG_STATE: {
            timeoutDiagXCallStateStopAndWaitArq();
            break;
        }
        default:
            _logger.warning("%s", fmt::format("exec_xcall_timeout:Get a command ID:{} but NO processing!", pCmd->getid()));
            break;
    }
}
// >> [LE0-9412]
void CxCallCmd::execTimeout(Command::Ptr pCmd)
{
	Command::Ptr p_original_cmd;
	if(CHD.checkCmdFromWaitQ(pCmd, p_original_cmd))
	{
		// IDs for command you sent before.
		switch ((pCmd->getid()/1000)*1000) // << [LE0-9412] 20240109 CK >>
		{
            // << [LE0-15085] 20240719 JasonKHLee
            case MCU_CMD_BASE:{
                _logger.information("execTimeout:MCU_CMD_BASE");
                exec_mcu_timeout(pCmd);
                break;
            }
            // << [LE0-9412] 20240109 CK
            case XCALL_CMD_BASE:{
                _logger.information("execTimeout:XCALL_CMD_BASE");
                exec_xcall_timeout(pCmd);
                break;
            }
            // >> [LE0-9412]
            case LCM_STATE_QUERY:
            {
                // << [LE022-1190] 20240205 CK
                if (m_pxCallManager->pxCallMachine()->internal_data.LIFE_CYCLE_STATE == LCM_STATE_INACTIVE) {
                    _logger.notice("execTimeout: LCM_STATE_QUERY and LCM state is inactive , to re-query LCM state");
                    sendstringcmd(static_cast<uint32_t>(LCM_TYPE), static_cast<uint32_t>(LCM_STATE_QUERY), static_cast<uint16_t>(NORMAL), std::string("GetStatus"), 10);
                }
                // >> [LE022-1190]
                break;
            }
			default:
				_logger.warning("Get a TIMEOUT ID but NO processing!");
				break;
		}
		CHD.clearForReply(pCmd);
	}
	else
	{
		_logger.warning("Receive a TIMEOUT not in wait queue!");
	}
}

//<< [LE0-6558] TracyTu,Send XCALL_DRIVING CANCEL_ECALL success when not trigger test aCall
std::string CxCallCmd::build_routine_ctl_rsp(std::string in_packet, bool is_handled_ok, uint8_t routine_status, uint8_t nrcCode)
{
    std::string data{};
    if(is_handled_ok)
    {
       data = in_packet.substr(6U, 4U); 
       (void)data.replace(0U, 1U, 1U, static_cast<char>(0x71));
       data.push_back(static_cast<char>(routine_status));
    }
    else
    {
        data.push_back(static_cast<char>(0x7F));
        data.push_back(static_cast<char>(0x31));
        data.push_back(static_cast<char>(nrcCode));
    }
    _logger.notice("%s", fmt::format("build_routine_ctl_rsp in_packet:{}, is_handled_ok:{}, routine_status:{}, data:{}", converToHex(in_packet), is_handled_ok, routine_status, converToHex(data)));
    return data;
}
//>> [LE0-6558] 

bool CxCallCmd::handle_routine_ctl_req(std::string in_packet, std::string &out_packet)
{
    //in_packet: (1) memory is "byte" array ("not ascii" string), (2) include header info size = 5(i.e. index 1~5) + data (i.e. index 6~N)
    bool ret = false;
	string SA = in_packet.substr(1, 2);
    string TA = in_packet.substr(3, 2);
    string header = in_packet.substr(1, 5);
    header.replace(0, 2, TA);
    header.replace(2, 2, SA);

    //<< [LE0-6558] TracyTu,Send XCALL_DRIVING CANCEL_ECALL success when not trigger test aCall >>
    //LE0-7328
    std::string data{}; 
    string temp = in_packet.substr(7, 1);
    int action = *(uint8_t *)temp.c_str();
	_logger.notice("%s", fmt::format("handle_routine_ctl_req in_packet:{}, action:{}", converToHex(in_packet), action));
	uint8_t rc_result = 1;
    if (action == 1)
    {
        rc_result = 1;
        std::string data_raw = in_packet.substr(10,1);
		uint8_t data_hex = *(uint8_t *)data_raw.c_str();
        std::bitset<8U> data_bit_set{data_hex};

        auto is_DID_matched  = [](const std::bitset<8U>& data_bit_set){ 
            auto contains_ecall_DID = !data_bit_set.test(1)||!data_bit_set.test(4)||!data_bit_set.test(5);
            auto contains_xcall_DID = !data_bit_set.test(0);
            return !contains_ecall_DID && contains_xcall_DID;};
        
        auto is_DID_conflict = [](const std::bitset<8U>& data_bit_set) {
            auto contains_ecall_DID = !data_bit_set.test(1)||!data_bit_set.test(4)||!data_bit_set.test(5);
            auto contains_xcall_DID = !data_bit_set.test(0);
            return contains_ecall_DID && contains_xcall_DID;}; //<< [LE0-7774] TracyTu >>

        _logger.notice("%s", fmt::format("handle_routine_ctl_req is_DID_matched:{}, is_DID_conflict:{}", is_DID_matched(data_bit_set), is_DID_conflict(data_bit_set)));
     
		_logger.warning("%s", fmt::format("data:{} data:{}", in_packet, data_hex));
        if(is_DID_matched(data_bit_set))
        {
            xCallDrivingResult = false;
            isNeedToResponseForXCallDriving = true;
            if(!data_bit_set.test(0)/*(data_hex | 0xFE) == 0xFE*/){
                _logger.notice("%s", fmt::format("state:{} sub_state:{}", m_pxCallManager->pxCallMachine()->state, m_pxCallManager->pxCallMachine()->sub_state));
                if((m_pxCallManager->pxCallMachine()->state == bCallState::BCALL_WAITING_FOR_CALLBACK) 
                    && (m_pxCallManager->pxCallMachine()->sub_state == bCallState::BCALL_WCB_BCALL_IDLE))
                {
                    xCallDrivingResult = true;
                    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_DIAG_REQ_CANCEL_BCALL), std::string(""));
                }
                else
                {
                    _logger.notice("%s", fmt::format("handle_routine_ctl_req CANCEL_ACALL failed!"));
                    xCallDrivingResult = false;
                }         
            }     

            data = build_routine_ctl_rsp(in_packet, xCallDrivingResult, rc_result);  	
            _logger.notice("%s", fmt::format("[is_DID_matched]handle_routine_ctl_req data:{}", converToHex(data)));
            out_packet.append(header);
            out_packet.append(data);
            ret = true;
        }
        else if(is_DID_conflict(data_bit_set))
        {
            xCallDrivingResult = false;
            isNeedToResponseForXCallDriving = true;

            //<< [LE0-7774] TracyTu
            const auto count_enable_bits = [](const bool val_a, const bool val_b, const bool val_c, const bool val_d){
                uint8_t count = 0;
                if(!val_a) count++; 
                if(!val_b) count++; 
                if(!val_c) count++; 
                if(!val_d) count++; 
                return count;
            };

            _logger.notice("%s", fmt::format("handle_routine_ctl_req count_enable_bits:{}", 
                count_enable_bits(data_bit_set.test(0), data_bit_set.test(1), data_bit_set.test(4), data_bit_set.test(5))));

            uint8_t nrcCode = NRC22;
            if(count_enable_bits(data_bit_set.test(0), data_bit_set.test(1), data_bit_set.test(4), data_bit_set.test(5))>1)
            {
                nrcCode = NRC24;                       
            }

            data = build_routine_ctl_rsp(in_packet, xCallDrivingResult, rc_result, nrcCode);  
            //>> [LE0-7774]
            _logger.notice("%s", fmt::format("[is_DID_conflict]handle_routine_ctl_req data:{}", converToHex(data)));
            (void)out_packet.append(header);
            (void)out_packet.append(data);
            ret = true;
        } 
        else
        {
            xCallDrivingResult = false;
            isNeedToResponseForXCallDriving = false;
            _logger.notice("%s", fmt::format("Get a data:{} but NO processing!",data_hex));
        }  
    }
    /*** No this case
	else if(action == 2)
    {
        rc_result = 2;
        data = build_routine_ctl_rsp(in_packet, false, rc_result);  		
	    out_packet.append(header);
        out_packet.append(data);
		ret = true;
    }
    ***/
    else if(action == 3)
    {
        if(isNeedToResponseForXCallDriving){
            rc_result = xCallDrivingResult? 2U : 3U;
            data = build_routine_ctl_rsp(in_packet, true, rc_result); 		
	        out_packet.append(header);
            out_packet.append(data);
		    ret = true;
        }
    }
    //>> [LE0-6558]    
	
	return ret;
}
// << [LE0WNC-9033] 20230217 CK
std::string CxCallCmd::converToHex(std::string hexValue, size_t size) {
    std::stringstream str;
    str.setf(std::ios_base::hex, std::ios::basefield);
    str.setf(std::ios_base::uppercase);
    str.fill('0');

    for (size_t i = 0; i < size; ++i) {
        str << " " << std::setw(2) << (unsigned short)(byte)hexValue[i];
    }
    return str.str();
}
std::string CxCallCmd::converToHex(std::string hexValue) {
    return converToHex(hexValue, hexValue.size());
}
// >> [LE0WNC-9033]
// << [LE0-5327] 20230606 CK
std::string CxCallCmd::decodeDMSmsNumberPayload(std::string payload) {
    // DM smsnumber payload example: "0='0770318694'" or "0770318694"
    std::size_t found1 = payload.find_first_of("'", 0);
    if (found1 == std::string::npos) {
        return payload;
    }
    std::size_t found2 = payload.find_first_of("'", found1 + 1);
    if (found2 == std::string::npos) {
        return payload;
    }
    return payload.substr(found1 + 1, found2 - found1 - 1);
}
// >> [LE0-5327]

void CxCallCmd::handle_aCall_cmd_XCALL_BCALL_BTN_PRESS() {
    _logger.information("exec_xcall_Cmd:XCALL_BCALL_BTN_PRESS");
    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_FLOW_BTN_PRESS), std::string(""));  //<< [LE0-6399] TracyTu, Play beep after pressing the button for two seconds >>
}
void CxCallCmd::handle_aCall_cmd_XCALL_BCALL_BTN_RELEASE() {
    _logger.information("exec_xcall_Cmd:XCALL_BCALL_BTN_RELEASE");
    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_FLOW_BTN_RELEASE), std::string(""));  //<< [LE0-6399] TracyTu, Play beep after pressing the button for two seconds >>
    std::string text(ACALL_FEEDBACK_PROMPTTEXT_RELEASED_PUSH);
    m_pxCallManager->pxCallWSocket()->promptText.notify(this, text);
}
void CxCallCmd::handle_aCall_cmd_XCALL_EM_DEBUG_STATE(Command::Ptr pCmd) {
    _logger.information("exec_xcall_Cmd:XCALL_EM_DEBUG_STATE");
    m_pxCallManager->pxCalldebug()->xcall_debug = pCmd->getdata();
    _logger.information("%s", fmt::format("xcall_debug:{}", m_pxCallManager->pxCalldebug()->xcall_debug));
}
void CxCallCmd::handle_aCall_cmd_XCALL_EM_AWAKENING_NOTIFY(Command::Ptr pCmd) {
    _logger.information("exec_xcall_Cmd:XCALL_EM_AWAKENING_NOTIFY");
    m_pxCallManager->pxCalldebug()->set_awakening_acall_configure();
    m_pxCallManager->pxCallRemoteClient()->setRemoteCommand(pCmd->getid(), pCmd->getdata());
    // << [LE0WNC-2444] 20230103 ZelosZSCao
    m_pxCallManager->pxCallMachine()->bcall_service_ready = true;
    m_pxCallManager->pxCallHandler()->xcall_check_crash_recovery();
    // >> [LE0WNC-2444]
}
void CxCallCmd::handle_aCall_cmd_XCALL_BCALL_MANUAL_TRIGGER() {
    _logger.information("exec_xcall_Cmd:XCALL_BCALL_MANUAL_TRIGGER");
    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_FLOW_MANUAL_TRIGGER), std::string("MANUAL")); //<< [LE0-15579] TracyTu, Move save config file to handler thread >> //<< [LE022-4538] TracyTu, xCall trace develop >>
}
void CxCallCmd::handle_aCall_cmd_XCALL_BCALL_BTN_LONG_PRESS() {
    _logger.notice("exec_xcall_Cmd:XCALL_BCALL_BTN_LONG_PRESS");
    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_FLOW_BTN_LONG_PRESS), std::string("MANUAL")); //<< [LE0-15579] TracyTu, Move save config file to handler thread >> //<< [LE022-4538] TracyTu, xCall trace develop >>
}
void CxCallCmd::handle_aCall_cmd_XCALL_BCALL_AUTO_TRIGGER() {
    _logger.information("exec_xcall_Cmd:XCALL_BCALL_AUTO_TRIGGER");
    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_FLOW_AUTO_TRIGGER), std::string("AUTO")); //<< [LE0-15579] TracyTu, Move save config file to handler thread >> //<< [LE022-4538] TracyTu, xCall trace develop >>
}
void CxCallCmd::handle_aCall_cmd_XCALL_BCALL_CALL_BACK() {
    _logger.information("exec_xcall_Cmd:XCALL_BCALL_CALL_BACK");
    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_FLOW_CALL_BACK), std::string(""));
}
void CxCallCmd::handle_aCall_cmd_XCALL_BCALL_MANUAL_CANCEL() {
    _logger.information("exec_xcall_Cmd:XCALL_BCALL_MANUAL_CANCEL");
    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_FLOW_MANUAL_CANCEL), std::string(""));
}
void CxCallCmd::handle_aCall_cmd_XCALL_BCALL_CALLOUT_SUCCESS() {
    _logger.information("exec_bcall_Cmd:XCALL_BCALL_CALLOUT_SUCCESS");
    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_FLOW_CALLOUT_SUCCESS), std::string(""));
}
void CxCallCmd::handle_aCall_cmd_XCALL_BCALL_CALLOUT_FAIL() {
    _logger.information("exec_bcall_Cmd:XCALL_BCALL_CALLOUT_FAIL");
    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_FLOW_CALLOUT_FAIL), std::string(""));
}
void CxCallCmd::handle_aCall_cmd_XCALL_BCALL_REGISTRATION_SUCCESS(Command::Ptr pCmd) {
    _logger.information("exec_bcall_Cmd:XCALL_BCALL_REGISTRATION_SUCCESS");
    m_pxCallManager->pxCallHandler()->putAnyToStateQ(BCALL_CELLULAR_REGISTRATION_STATE, pCmd->getAnydata());  // << [RTBMVAL-785][LE0-5946] 20230627 CK >>
}
void CxCallCmd::handle_aCall_cmd_XCALL_BCALL_MSD_TRANSFER_SUCCESS() {
    _logger.information("exec_bcall_Cmd:XCALL_BCALL_MSD_TRANSFER_SUCCESS");
    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_FLOW_MSD_TRANSFER_SUCCESS), std::string(""));
}
void CxCallCmd::handle_aCall_cmd_XCALL_BCALL_MSD_REQUEST(Command::Ptr pCmd) {
    _logger.information("exec_bcall_Cmd:XCALL_BCALL_MSD_REQUEST");
    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_FLOW_MSD_REQUEST), std::string(""));
    sendstringcmd(pCmd->getFrom(), pCmd->getid(), REPLY, pCmd->getdata());
}
void CxCallCmd::handle_aCall_cmd_XCALL_BCALL_BTN_SHORT_PRESS() {
    _logger.information("exec_bcall_Cmd:XCALL_BCALL_BTN_SHORT_PRESS");
    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_FLOW_BTN_SHORT_PRESS), std::string(""));
}
// << [LE0WNC-1952] 20230512 CK
void CxCallCmd::handle_aCall_cmd_XCALL_STATE_ECALL_WAITING_FOR_CALLBACK() {
    _logger.information("exec_bcall_Cmd:XCALL_STATE_ECALL_WAITING_FOR_CALLBACK");
    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_ECALL_STATE_WAITING_FOR_CALLBACK), std::string(""));
}
void CxCallCmd::handle_aCall_cmd_XCALL_STATE_ECALL_VOICE_COMMUNICATION() {
    _logger.information("exec_bcall_Cmd:XCALL_STATE_ECALL_VOICE_COMMUNICATION");
    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_ECALL_STATE_VOICE_COMMUNICATION), std::string(""));
}
// >> [LE0WNC-1952]
void CxCallCmd::handle_aCall_cmd_XCALL_STATE_ECALL_TRIGGERED() {
    _logger.information("exec_bcall_Cmd:XCALL_STATE_ECALL_TRIGGERED");
    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_ECALL_STATE_TRIGGERED), std::string(""));
}
void CxCallCmd::handle_aCall_cmd_XCALL_STATE_ECALL_OFF() {
    _logger.information("exec_bcall_Cmd:XCALL_STATE_ECALL_OFF");
    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_ECALL_STATE_OFF), std::string(""));
}
void CxCallCmd::handle_aCall_cmd_XCALL_ECALL_ROUTINE_CTL_REQ(Command::Ptr pCmd) {
    _logger.information("exec_bcall_Cmd:XCALL_ECALL_ROUTINE_CTL_REQ");
    std::string out_packet;
    bool ret = handle_routine_ctl_req(pCmd->getdata(), out_packet);
    if (true == ret)
        sendstringcmd(static_cast<uint32_t>(MCUMANAGER_TYPE), static_cast<uint32_t>(MCUM_UDS_ROUTINE_CTL_RESPONSE), static_cast<uint16_t>(NORMAL), out_packet);
}
// << [LE0-7867][LE0-7868][LE0-7863] 20230906 CK
void CxCallCmd::handle_aCall_cmd_XCALL_ECALL_ROUTINE_CTL_ABORT(Command::Ptr pCmd) {
    _logger.notice("%s", fmt::format("exec_ecall_Cmd:XCALL_ECALL_ROUTINE_CTL_ABORT data:{}", pCmd->getdata())); //DIAG_ABORT_REASON::SAFETY_CONDITION_NOT_MET/SESSION_CHANGED
}
// >> [LE0-7867][LE0-7868][LE0-7863]
void CxCallCmd::handle_aCall_cmd_XCALL_BACKUP_BATT_NOTIFY(Command::Ptr pCmd) {
    _logger.information("exec_bcall_Cmd:XCALL_BACKUP_BATT_NOTIFY");
    _logger.information("%s", fmt::format("XCALL_BACKUP_BATT_NOTIFY:{}", pCmd->getdata()));
    m_pxCallManager->pxCallMachine()->m_back_up_battery = pCmd->getdata();
}
void CxCallCmd::handle_aCall_cmd_XCALL_STATE_BCALL_CALLBACK_TIMEOUT() {
    _logger.information("exec_bcall_Cmd:XCALL_STATE_BCALL_CALLBACK_TIMEOUT");
    if (bCallState::BCALL_WAITING_FOR_CALLBACK == m_pxCallManager->pxCallMachine()->state) {
        m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_FLOW_CALL_BACK_TIMEOUT), std::string(""));
    }
}
void CxCallCmd::handle_aCall_cmd_XCALL_STATE_ECALL_CALLBACK_TIMEOUT(Command::Ptr pCmd) {
    _logger.information("exec_bcall_Cmd:XCALL_STATE_ECALL_CALLBACK_TIMEOUT");
    m_pxCallManager->pxCallRemoteClient()->setRemoteCommand(pCmd->getid(), pCmd->getdata());
}
void CxCallCmd::handle_aCall_cmd_XCALL_BCALL_DM_ACALL_SMS_NUMBER(Command::Ptr pCmd) {
    _logger.information("exec_bcall_Cmd:XCALL_BCALL_DM_ACALL_SMS_NUMBER");
    std::string smsNumber = decodeDMSmsNumberPayload(pCmd->getdata());                                                                                        // << [LE0-5327] 20230606 CK >>
    _logger.notice("%s", fmt::format("XCALL_BCALL_DM_ACALL_SMS_NUMBER from:{}, data:{}, BCALL_SMS_NUMBER:{}", pCmd->getFrom(), pCmd->getdata(), smsNumber));  // << [LE0WNC-10142] 20230425 CK >>
    // << [LE0-18383] 20250207 CK
    if (m_pxCallManager->pxCallMachine()->isValidSMSNumber(smsNumber)) {
        m_pxCallManager->pxCallMachine()->config.BCALL_SMS_NUMBER = smsNumber; // << [LE0-5327] 20230606 CK >>
        m_pxCallManager->pxCallXmlConfig()->saveAcallSmsNumber(smsNumber);
    } else {
        m_pxCallManager->pxCallTrace()->setBBSReceivedInvalidSMSNumber(invalidSmsNumberReceivedFrom::receivedFromDM);
    }
    // >> [LE0-18383]
}
void CxCallCmd::handle_aCall_cmd_XCALL_ECALL_BTN_event(Command::Ptr pCmd) {
    _logger.information("%s", fmt::format("exec_bcall_Cmd:{}", pCmd->getid()));
    m_pxCallManager->pxCallMachine()->onBtnStateChanged(pCmd->getid());
}
// << [LE0-6295] 20230726 CK
void CxCallCmd::handle_aCall_cmd_XCALL_NEED_SYNC_BCALL_TO_ECALL() {
    _logger.notice("%s", fmt::format("exec_bcall_Cmd:XCALL_NEED_SYNC_BCALL_TO_ECALL"));
    m_pxCallManager->pxCallRemoteClient()->syncStateToECall(std::string("init"));
}
// >> [LE0-6295]
void CxCallCmd::handle_aCall_cmd_XCALL_BCALL_HTTP_MESSAGE_RESPONSE(Command::Ptr pCmd) {
    _logger.notice("%s", fmt::format("exec_bcall_Cmd:XCALL_BCALL_HTTP_MESSAGE_RESPONSE"));
    Parser p1;
    Var result = p1.parse(pCmd->cmd_data);
    Object::Ptr pObject = result.extract<Object::Ptr>();
    bool isSuccess = pObject->getValue<bool>("isSuccess");
    bool isAbortSendMessage = pObject->getValue<bool>("isAbortSendMessage");
    long int sendInfoCrashChangedAt = pObject->getValue<long int>("sendInfoCrashChangedAt");
    if (!isAbortSendMessage) {
        m_pxCallManager->pxCallHandler()->putAnyToStateQ(BCALL_FLOW_HTTPS_TRANSFER_FINISHED, isSuccess);
    }
    if (isSuccess) {
        // REQ-0481854 erase after the transmission by HTTP (or SMS)
        m_pxCallManager->pMessage()->cleanInfoCrashAfterTransmission(sendInfoCrashChangedAt);  // << [LE0WNC-10115] 20230509 CK >>
    }
}
//<< [LE0-11230]TracyTu, TPS ecall requirement 
void CxCallCmd::handle_aCall_cmd_XCALL_TPSECALL_SMS_REQUEST(Command::Ptr pCmd) {
    _logger.information("exec_bcall_Cmd:XCALL_TPSECALL_SMS_REQUEST");
    m_pxCallManager->pxCallRemoteClient()->setRemoteCommand(pCmd->getid(), pCmd->getdata()); 
}

void CxCallCmd::handle_aCall_cmd_XCALL_TPSECALL_DM_ECALL_SMS_NUMBER(Command::Ptr pCmd) {
    _logger.information("exec_bcall_Cmd:XCALL_TPSECALL_DM_ECALL_SMS_NUMBER");
    m_pxCallManager->pxCallRemoteClient()->setRemoteCommand(pCmd->getid(), pCmd->getdata()); 
}
//>> [LE0-11230]
void CxCallCmd::handle_aCall_cmd_XCALL_DRIVING_DIAG_STATE_UPDATE(Command::Ptr pCmd) {
    _logger.notice("exec_bcall_Cmd:XCALL_DRIVING_DIAG_STATE_UPDATE");
    // need handle in cmd thread
    if ((pCmd->cmd_from == XCALL_TYPE) && (pCmd->cmd_id == XCALL_DRIVING_DIAG_SERVICE_UPDATE) && (pCmd->any_data.type() == typeid(DIAG_CONDITION_XCALL_DRIVING))) {
        DIAG_CONDITION_XCALL_DRIVING diagConditionXCallDriving = std::any_cast<DIAG_CONDITION_XCALL_DRIVING>(pCmd->any_data);
    
          _logger.notice("%s", fmt::format("handle_eCall_cmd_XCALL_DIAG_STATE_UPDATE mDiagConditionXCallState:{} diagConditionXCallState:{}",
                                         xCallDrivingArqCurrent.mDiagConditionXCallDriving, diagConditionXCallDriving));

        if (xCallDrivingArqCurrent.mDiagConditionXCallDriving != diagConditionXCallDriving) {
            xCallDrivingArqCurrent.mSerialNumber = static_cast<uint8_t>(xCallDrivingArqOut.mSerialNumber + 1); //keep current SN to out SN + 1
            xCallDrivingArqCurrent.mDiagConditionXCallDriving = diagConditionXCallDriving;

            syncDiagXCallDrivingStopAndWaitArq();
        }
    } else {
        _logger.error("%s", fmt::format("handle_eCall_cmd_XCALL_DIAG_STATE_UPDATE invalid cmd, cmd_from:{} cmd_id:{}", pCmd->cmd_from, pCmd->cmd_id));
    }
}
// << [LE0-9412] 20240109 CK
void CxCallCmd::handle_aCall_cmd_XCALL_DIAG_STATE_UPDATE(Command::Ptr pCmd) {
    _logger.information("exec_bcall_Cmd:XCALL_DIAG_STATE_UPDATE");
    // need handle in cmd thread
    if ((pCmd->cmd_from == XCALL_TYPE) && (pCmd->cmd_id == XCALL_DIAG_STATE_UPDATE) && (pCmd->any_data.type() == typeid(DIAG_CONDITION_XXCALL_STATE))) {
        DIAG_CONDITION_XXCALL_STATE diagConditionXCallState = std::any_cast<DIAG_CONDITION_XXCALL_STATE>(pCmd->any_data);

        _logger.notice("%s", fmt::format("handle_aCall_cmd_XCALL_DIAG_STATE_UPDATE mDiagConditionXCallState:{} diagConditionXCallState:{}",
                                         xCallStateArqCurrent.mDiagConditionXCallState, diagConditionXCallState));

        if (xCallStateArqCurrent.mDiagConditionXCallState != diagConditionXCallState) {
            xCallStateArqCurrent.mSerialNumber = static_cast<uint8_t>(xCallStateArqOut.mSerialNumber + 1); //keep current SN to out SN + 1
            xCallStateArqCurrent.mDiagConditionXCallState = diagConditionXCallState;

            syncDiagXCallStateStopAndWaitArq();
        }
    } else {
        _logger.error("%s", fmt::format("handle_aCall_cmd_XCALL_DIAG_STATE_UPDATE invalid cmd, cmd_from:{} cmd_id:{}", pCmd->cmd_from, pCmd->cmd_id));
    }
}
void CxCallCmd::handle_aCall_cmd_XCALL_DIAG_STATE_NEED_SYNC(Command::Ptr pCmd) {
    _logger.information("exec_bcall_Cmd:XCALL_DIAG_STATE_NEED_SYNC");
    syncDiagXCallStateStopAndWaitArq();
}
// << [LE0-11009][LE0-11039][LE022-983] 20240301 CK
void CxCallCmd::handle_aCall_cmd_XCALL_DTC_CHANGE_NOTIFY(Command::Ptr pCmd) {
    _logger.information("exec_bcall_Cmd:XCALL_DTC_CHANGE_NOTIFY");
    const std::string str = pCmd->getdata();
    const std::size_t index = str.find(",", 0);
    if ((index != string::npos)) {
        const std::string dtcCodeStr = str.substr(0, index);
        const std::string dtcStateStr = str.substr(index + 1);
        _logger.notice("%s", fmt::format("handle_aCall_cmd_XCALL_DTC_CHANGE_NOTIFY dtcCode:{} dtcState:{}", dtcCodeStr, dtcStateStr));
    } else {
        _logger.error("%s", fmt::format("handle_aCall_cmd_XCALL_DTC_CHANGE_NOTIFY invalid data:{}", str));
    }
}
// >> [LE0-11009][LE0-11039][LE022-983]

// << [LE0-12307] 20240402 EasonCCLiao
void CxCallCmd::handle_aCall_cmd_XCALL_ECALL_BTN_BLOCKED(Command::Ptr pCmd) {
    _logger.error("%s", fmt::format("exec_bcall_Cmd:XCALL_ECALL_BTN_BLOCKED ECall Button Block"));
    DLT_BBOX_LOG("%u, %u", BBS_XCALL_EVENT, BBS_EVENT_ECALL_BTN_BLOCK); //<< [LE022-4538] TracyTu, xCall trace develop >>
}
void CxCallCmd::handle_aCall_cmd_XCALL_BCALL_BTN_BLOCKED(Command::Ptr pCmd) {
    _logger.error("%s", fmt::format("exec_bcall_Cmd:XCALL_BCALL_BTN_BLOCKED ACall Button Block"));
    m_pxCallManager->pxCallHandler()->putStringToStateQ(BCALL_FLOW_BTN_BLOCKED, std::string(""));  //<< [LE022-4390] TracyTu, Move save config file to handler thread >>
    DLT_BBOX_LOG("%u,%u", BBS_XCALL_EVENT, BBS_EVENT_ACALL_BTN_BLOCK); //<< [LE022-4538] TracyTu, xCall trace develop >>
}
// >> [LE0-12307]

// << [LE022-1667] TracyTu, Support one image 
void CxCallCmd::handle_aCall_cmd_XCALL_DID_CHANGE_NOTIFY(Command::Ptr pCmd) {
    const std::string DIDStr = pCmd->getdata();
    _logger.notice("%s", fmt::format("exec_bcall_Cmd:handle_aCall_cmd_XCALL_DID_CHANGE_NOTIFY:{}", DIDStr));
    if(DIDStr.compare(DID_2103_TEL_FCT_XCALL) == 0)
    {
        m_pxCallManager->pxCallHwValidator()->checkAcallPushPresence();
    }
}
// >> [LE022-1667] 
// << [LE022-2567] 20240515 CK
void CxCallCmd::handle_aCall_cmd_XCALL_ACALL_ECALL_CONNECTED(Command::Ptr pCmd) {
    _logger.notice("%s", fmt::format("exec_bcall_Cmd:XCALL_ACALL_ECALL_CONNECTED"));
    mIsECallConnected = true;

    AutoPtr<Notification> pNf(waitECallConnectedCmdQueue.dequeueNotification());
    while(pNf) {
        Command::Ptr pCmdPending = pNf.cast<Command>();
        poco_assert(pCmdPending.get());
        CHD.cmd_queue.enqueueNotification(pCmdPending);

        pNf = waitECallConnectedCmdQueue.dequeueNotification();
    }
}
void CxCallCmd::handle_aCall_cmd_XCALL_ACALL_ECALL_DISCONNECTED(Command::Ptr pCmd) {
    _logger.notice("%s", fmt::format("exec_bcall_Cmd:XCALL_ACALL_ECALL_DISCONNECTED"));
    mIsECallConnected = false;
}
// >> [LE022-2567]
// << [LE022-2739] 20240528 CK
void CxCallCmd::handle_aCall_cmd_XCALL_BCALL_RECOVERY_CALL_CRASH(Command::Ptr pCmd){
    _logger.notice("%s", fmt::format("exec_bcall_Cmd:XCALL_BCALL_RECOVERY_CALL_CRASH"));
    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_FLOW_CALL_CRASH_RECOVERY_IND), std::string(""));
}
void CxCallCmd::handle_aCall_cmd_XCALL_BCALL_RECOVERY_PRESS_CRASH(Command::Ptr pCmd){
    _logger.notice("%s", fmt::format("exec_bcall_Cmd:XCALL_BCALL_RECOVERY_PRESS_CRASH"));
    m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_FLOW_PRESS_CRASH_RECOVERY_IND), std::string(""));
}
// >> [LE022-2739]

// << [LE022-3863] 20240730 ZelosZSCao
void CxCallCmd::handle_aCall_cmd_XCALL_ECALL_URC_DEMOAPP_STATUS_IND(Command::Ptr pCmd){
    _logger.notice("exec DemoApp2Ready");
    m_pxCallManager->notifyDemoappState();
}
// >> [LE022-3863]

void CxCallCmd::updateDiagXCallState(DIAG_CONDITION_XXCALL_STATE diagConditionXCallState) {
    _logger.notice("%s", fmt::format("updateDiagXCallState diagConditionXCallState:{}", diagConditionXCallState));
    enqueueAnyCmd(XCALL_DIAG_STATE_UPDATE, static_cast<uint16_t>(NORMAL), diagConditionXCallState);
}

void CxCallCmd::updateDiag_xcall_driving_State(DIAG_CONDITION_XCALL_DRIVING diagConditionXCALL_Driving_State) {
    _logger.notice("%s", fmt::format("updateDiag_xcall_driving_State diagConditionXCALL_Driving_State:{}", diagConditionXCALL_Driving_State));
    enqueueAnyCmd(XCALL_DRIVING_DIAG_SERVICE_UPDATE, NORMAL, diagConditionXCALL_Driving_State);
}

void CxCallCmd::syncDiagXCallStateStopAndWaitArq() {
    // need handle in cmd thread
    const uint32_t cmdType = MCUMANAGER_TYPE;
    const uint32_t cmdId = XCALL_SET_DIAG_STATE;
    const uint16_t cmdFlag = NORMAL;
    const unsigned int arqTimeout = 2;  // sec, because Uart send and ack normal less than 0.5 sec
    const uint8_t owner_id = 0x0;

    Command::Ptr p_original_cmd = nullptr;
    Command::Ptr pCmdOut = new Command(cmdType, cmdId, cmdFlag, std::string(""));
    const bool isSyncing = CHD.checkCmdFromWaitQ(pCmdOut, p_original_cmd);

    _logger.notice("%s", fmt::format("syncDiagXCallStateStopAndWaitArq isSyncing:{} inSn:{} inData:{} outSn:{} OutData:{} currSn:{} currData:{}",
                                     isSyncing,
                                     xCallStateArqIn.mSerialNumber, xCallStateArqIn.mDiagConditionXCallState,
                                     xCallStateArqOut.mSerialNumber, xCallStateArqOut.mDiagConditionXCallState,
                                     xCallStateArqCurrent.mSerialNumber, xCallStateArqCurrent.mDiagConditionXCallState));

    if (isSyncing) {
        // waiting for sync
    } else {
        const bool isArqOutSynced = (xCallStateArqOut == xCallStateArqIn);
        const bool isArqCurrentSynced = (xCallStateArqCurrent == xCallStateArqOut);
        if (isArqOutSynced && (!isArqCurrentSynced)) {
            xCallStateArqOut = xCallStateArqCurrent;
        }
        string cmdData;
        cmdData.push_back(owner_id);
        cmdData.push_back(xCallStateArqOut.mSerialNumber);
        cmdData.push_back(static_cast<uint8_t>(xCallStateArqOut.mDiagConditionXCallState));
        sendstringcmd(cmdType, cmdId, cmdFlag, cmdData, arqTimeout);
    }
}
// << [LE0-15085] JasonKHLee 
bool CxCallCmd::receiveAckDiagXCallDrivingStopAndWaitArq(Command::Ptr pCmd) {
    // need handle in cmd thread
    const std::string cmdData = pCmd->cmd_data;
    try {
        _logger.notice("%s", fmt::format("receiveAckDiagXCallDrivingStopAndWaitArq data.size:{} data(hex):{}", cmdData.size(), converToHex(cmdData)));
        xCallDrivingArqIn.mSerialNumber = (uint8_t)cmdData.at(1);
        xCallDrivingArqIn.mDiagConditionXCallDriving = static_cast<DIAG_CONDITION_XCALL_DRIVING>((uint8_t)cmdData.at(2));
    } catch (Poco::Exception& exc) {
        _logger.error("%s", fmt::format("receiveAckDiagXCallDrivingStopAndWaitArq fail, e::{}", exc.displayText()));
    } catch (...) {
        _logger.error("%s", fmt::format("receiveAckDiagXCallDrivingStopAndWaitArq Exception"));
    }

    const bool isArqOutSynced = (xCallDrivingArqOut == xCallDrivingArqIn);
    const bool isArqCurrentSynced = (xCallDrivingArqCurrent == xCallDrivingArqOut);
    if (isArqOutSynced && (!isArqCurrentSynced)) {
        enqueueStringCmd(XCALL_DRIVING_DIAG_STATE_NEED_SYNC, NORMAL, "");
    }
    return isArqOutSynced;
}
// >> [LE0-15085]
bool CxCallCmd::receiveAckDiagXCallStateStopAndWaitArq(Command::Ptr pCmd) {
    // need handle in cmd thread
    const std::string cmdData = pCmd->cmd_data;
    try {
        _logger.notice("%s", fmt::format("receiveAckDiagXCallStateStopAndWaitArq data.size:{} data(hex):{}", cmdData.size(), converToHex(cmdData)));
        xCallStateArqIn.mSerialNumber = (uint8_t)cmdData.at(1);
        xCallStateArqIn.mDiagConditionXCallState = static_cast<DIAG_CONDITION_XXCALL_STATE>((uint8_t)cmdData.at(2));
    } catch (Poco::Exception& exc) {
        _logger.error("%s", fmt::format("receiveAckDiagXCallStateStopAndWaitArq fail, e::{}", exc.displayText()));
    } catch (...) {
        _logger.error("%s", fmt::format("receiveAckDiagXCallStateStopAndWaitArq Exception"));
    }

    const bool isArqOutSynced = (xCallStateArqOut == xCallStateArqIn);
    const bool isArqCurrentSynced = (xCallStateArqCurrent == xCallStateArqOut);
    if (isArqOutSynced && (!isArqCurrentSynced)) {
        enqueueStringCmd(XCALL_DIAG_STATE_NEED_SYNC, static_cast<uint16_t>(NORMAL), "");
    }
    return isArqOutSynced;
}
void CxCallCmd::timeoutDiagXCallStateStopAndWaitArq() {
    // need handle in cmd thread
    _logger.notice("%s", fmt::format("timeoutDiagXCallStateStopAndWaitArq"));
    enqueueStringCmd(XCALL_DIAG_STATE_NEED_SYNC, static_cast<uint16_t>(NORMAL), "");
}
// >> [LE0-9412]
// << [LE0-15085] JasonKHLee 
void CxCallCmd::timeoutDiagXCallDrivingStopAndWaitArq() {
    // need handle in cmd thread
    _logger.notice("%s", fmt::format("timeoutDiagXCallDrivingStopAndWaitArq"));
    enqueueStringCmd(XCALL_DRIVING_DIAG_STATE_NEED_SYNC, NORMAL, "");
}
// >> [LE0-15085]

// << [LE0-15085] JasonKHLee 
void CxCallCmd::syncDiagXCallDrivingStopAndWaitArq() {
    // need handle in cmd thread
    const uint32_t cmdType = MCUMANAGER_TYPE;
    const uint32_t cmdId = SOC_SERVICE_SET_DIAG_STATE;
    const uint16_t cmdFlag = NORMAL;
    const unsigned int arqTimeout = 2;  // sec, because Uart send and ack normal less than 0.5 sec
    const uint8_t owner_id = 0x5;

    Command::Ptr p_original_cmd = nullptr;
    Command::Ptr pCmdOut = new Command(cmdType, cmdId, cmdFlag, std::string(""));
    const bool isSyncing = CHD.checkCmdFromWaitQ(pCmdOut, p_original_cmd);

    _logger.notice("%s", fmt::format("syncDiagXCallDrivingStopAndWaitArq isSyncing:{} inSn:{} inData:{} outSn:{} OutData:{} currSn:{} currData:{}",
                                     isSyncing,
                                     xCallDrivingArqIn.mSerialNumber, xCallDrivingArqIn.mDiagConditionXCallDriving,
                                     xCallDrivingArqOut.mSerialNumber, xCallDrivingArqOut.mDiagConditionXCallDriving,
                                     xCallDrivingArqCurrent.mSerialNumber, xCallDrivingArqCurrent.mDiagConditionXCallDriving));

    if (isSyncing) {
        // waiting for sync
    } else {
        const bool isArqOutSynced = (xCallDrivingArqOut == xCallDrivingArqIn);
        const bool isArqCurrentSynced = (xCallDrivingArqCurrent == xCallDrivingArqOut);
        if (isArqOutSynced && (!isArqCurrentSynced)) {
            xCallDrivingArqOut = xCallDrivingArqCurrent;
        }
        string cmdData;
        cmdData.push_back(owner_id);
        cmdData.push_back(xCallDrivingArqOut.mSerialNumber);
        cmdData.push_back(static_cast<uint8_t>(xCallDrivingArqOut.mDiagConditionXCallDriving));
        sendstringcmd(cmdType, cmdId, cmdFlag, cmdData, arqTimeout);
    }
}
// >> [LE0-15085]
// << [LE022-1667] TracyTu, Support one image 
bool CxCallCmd::can_be_exec(uint32_t cmdId)
{
    bool ret = false;
    std::vector<uint32_t> cmds = {XCALL_BCALL_BTN_PRESS, 
                                  XCALL_BCALL_BTN_RELEASE, 
                                  XCALL_BCALL_MANUAL_TRIGGER, 
                                  XCALL_BCALL_BTN_LONG_PRESS, 
                                  XCALL_BCALL_AUTO_TRIGGER};
    _logger.imp("%s", fmt::format("[AT] can_be_exec isAcallSupported:{}, cmdId:{}", m_pxCallManager->pxCallHwValidator()->isAcallSupported(), cmdId)); // << [LE022-5019] 20240925 EasonCCLiao >>
    if(m_pxCallManager->pxCallHwValidator()->isAcallSupported() 
        || std::none_of(cmds.begin(), cmds.end(), [&cmdId](const uint32_t& cmd)
        { return cmd == cmdId; }))
    {
        ret = true;
    }
    else
    {
        ret = false;
    }
    return ret;
}
// >> [LE022-1667]
// << [LE022-2739][LE022-2567] 20240515 CK
bool CxCallCmd::checkNeedCoverNEENotReadySituation(Command::Ptr pCmd) {
    bool isNeedCoverNEENotReadySituation = false;
    if (!mIsECallConnected) {
        // << [LE022-2544] 20240510 CK
        // need to cover NEE not ready situation
        // << [LE022-2679] 20240522 ck
        switch (pCmd->getid()) {
            case XCALL_BCALL_BTN_PRESS:
            case XCALL_BCALL_BTN_RELEASE:
            case XCALL_BCALL_BTN_SHORT_PRESS:
                isNeedCoverNEENotReadySituation = checkNeedCoverNEENotReadySituationACall(pCmd, false); // << [LE022-2739] 20240528 CK >>
                break;
            case XCALL_BCALL_BTN_LONG_PRESS:
            case XCALL_BCALL_MANUAL_TRIGGER:
            case XCALL_BCALL_AUTO_TRIGGER:
            case XCALL_BCALL_RECOVERY_CALL_CRASH:
            case XCALL_BCALL_RECOVERY_PRESS_CRASH:
                isNeedCoverNEENotReadySituation = checkNeedCoverNEENotReadySituationACall(pCmd, true); // << [LE022-2739] 20240528 CK >>
                break;
            case XCALL_ECALL_BTN_PRESS:
            case XCALL_ECALL_BTN_RELEASE:
            case XCALL_ECALL_BTN_SHORT_PRESS:
                isNeedCoverNEENotReadySituation = checkNeedCoverNEENotReadySituationECall(false); // << [LE022-4229][LE022-3985] 20240813 EasonCCLiao >>
                break;
            case XCALL_ECALL_BTN_LONG_PRESS:
                isNeedCoverNEENotReadySituation = checkNeedCoverNEENotReadySituationECall(true); // << [LE022-4229][LE022-3985] 20240813 EasonCCLiao >>
                break;
            default:
                isNeedCoverNEENotReadySituation = false;
                break;
        }
        // >> [LE022-2679]
        // >> [LE022-2544]
    }
    return isNeedCoverNEENotReadySituation;
}
// << [LE022-2679] 20240522 ck
bool CxCallCmd::checkNeedCoverNEENotReadySituationACall(Command::Ptr pCmd, const bool isTriggerCallCmd) {
    bool isNeedCoverNEENotReadySituation = false;
    const bool isACallSupported = m_pxCallManager->pxCallHwValidator()->isAcallSupported();
    const uint32_t cmdId = pCmd->getid();
    if (isACallSupported) {
        _logger.notice("%s", fmt::format("checkNeedCoverNEENotReadySituation cmdId:{}", cmdId));
        waitECallConnectedCmdQueue.enqueueNotification(pCmd);
        if (isTriggerCallCmd && m_pxCallManager->pxCallMachine()->checkManualACallCanTrigger()) {
            m_pxCallManager->pxCallHandler()->putStringToStateQ(static_cast<uint32_t>(BCALL_FLOW_ACALL_TRIGGERED_PENDING_NEE), std::string(""));
        }
        isNeedCoverNEENotReadySituation = true;
    }
    return isNeedCoverNEENotReadySituation;
}
// >> [LE022-2739][LE022-2567]
// << [LE022-4229][LE022-3985] 20240813 EasonCCLiao
bool CxCallCmd::checkNeedCoverNEENotReadySituationECall(const bool isTriggerCallCmd) {
    bool isNeedCoverNEENotReadySituation = false;
    const bool isEcallSupported = m_pxCallManager->pxCallHwValidator()->isEcallSupported();
    if (isEcallSupported) {
        if (isTriggerCallCmd) {
            m_pxCallManager->pxCallHandler()->putStringToStateQ(BCALL_FLOW_ECALL_TRIGGERED_PENDING_NEE, std::string(""));
        }
        isNeedCoverNEENotReadySituation = true;
    }
    return isNeedCoverNEENotReadySituation;
}
// >> [LE022-4229][LE022-3985]
// >> [LE022-2679]
}  // namespace MD::xCallManage
