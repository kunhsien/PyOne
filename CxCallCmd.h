#ifndef CXCALLCMD_H_
#define CXCALLCMD_H_
#include "Poco/Thread.h"
#include "Poco/Logger.h"
#include "common.h"
#include "Poco/OSP/BundleContext.h"
#include "Poco/Timestamp.h"
#include "CxCallThreadsAliveWatcher.h"
#include <unordered_map>
#include <functional>
#include "CLogger.h"

namespace MD::xCallManage {

typedef std::function<void(Command::Ptr)> execFunction;

class xCallManager;
class CxCallCmd: public Poco::Runnable, public CxCallThreadsAliveWatcher::Observation // << [LE0-5619] 20230615 CK >>
{
  // << [LE0-9412] 20240109 CK
  public:
    typedef enum {
        XXCALL_STATE_OFF = 0U,
        XXCALL_STATE_PROCESSING = 1U,
        XXCALL_STATE_WCB = 2U,
    } DIAG_CONDITION_XXCALL_STATE;

    typedef enum {
      XCALL_DRIVING_NOT_READY = 0U,
      XCALL_DRIVING_READY = 1U,
    } DIAG_CONDITION_XCALL_DRIVING;

  private:
    struct XCallStateStopAndWaitARQ {
        uint8_t mSerialNumber;
        DIAG_CONDITION_XXCALL_STATE mDiagConditionXCallState;

        inline bool operator==(const XCallStateStopAndWaitARQ& data) const {
            return (mSerialNumber == data.mSerialNumber) && (mDiagConditionXCallState == data.mDiagConditionXCallState);
        }
        inline void operator=(const XCallStateStopAndWaitARQ& data) {
            mSerialNumber = data.mSerialNumber;
            mDiagConditionXCallState = data.mDiagConditionXCallState;
        }
    };
 // >> [LE0-9412]
     struct XCallDrivingStopAndWaitARQ {
        uint8_t mSerialNumber;
        DIAG_CONDITION_XCALL_DRIVING mDiagConditionXCallDriving;

        inline bool operator==(const XCallDrivingStopAndWaitARQ& data) const {
            return (mSerialNumber == data.mSerialNumber) && (mDiagConditionXCallDriving == data.mDiagConditionXCallDriving);
        }
        inline void operator=(const XCallDrivingStopAndWaitARQ& data) {
            mSerialNumber = data.mSerialNumber;
            mDiagConditionXCallDriving = data.mDiagConditionXCallDriving;
        }
    };
public:
    CxCallCmd(Poco::OSP::BundleContext::Ptr pContext, xCallManager* xCallManager);
    virtual ~CxCallCmd();
    void start() override;
    void stop() override;
    bool isLcmSwmNeedAlive() override; // << [LE0-5619] 20230615 CK >>
    CxCallThreadsAliveWatcher::ThreadInfo* getThreadInfo() override; // << [LE0-5619] 20230615 CK >>
	void run() override;
	void sendstringcmd(uint32_t type, uint32_t id, uint16_t flag, std::string data, unsigned int timeout = 0);
	void sendvoidcmd(uint32_t type, uint32_t id, uint16_t flag, void* obj, unsigned int timeout = 0);
	void sendanycmd(uint32_t type, uint32_t id, uint16_t flag, std::any data, unsigned int timeout = 0);
    void enqueueStringCmd(uint32_t id, uint16_t flag, std::string data); // << [LE0-9412] 20240109 CK >>
    void enqueueAnyCmd(uint32_t id, uint16_t flag, std::any data); // << [LE0-9412] 20240109 CK >>
	void execCmd(Command::Ptr);
	void execReply(Command::Ptr);
	void execTimeout(Command::Ptr);
	void exec_conn_Cmd(Command::Ptr pCmd);
	void exec_lcm_Cmd(Command::Ptr pCmd);
    void exec_mcu_Cmd(Command::Ptr pCmd); // << [LE0-9412] 20240109 CK >>
	void exec_xcall_Cmd(Command::Ptr pCmd);
	void exec_cellular_Cmd(Command::Ptr pCmd);
	void exec_urc_Cmd(Command::Ptr pCmd); // << [LE0WNC-2771] 20230418 ZelosZSCao >>
    bool exec_dm_reply(Command::Ptr pCmd); // << [LE0-9412] 20240109 CK >>
    bool exec_conn_reply(Command::Ptr pCmd); // << [LE0-9412] 20240109 CK >>
	bool handle_routine_ctl_req(std::string in_packet, std::string &out_packet);
    bool exec_audio_reply(Command::Ptr pCmd); // << [LE0-9412] 20240109 CK >>
    bool exec_lcm_reply(Command::Ptr pCmd); // << [LE0-9412][LE0-5724] 20230619 CK >>
    bool exec_mcu_reply(Command::Ptr pCmd); // << [LE0-15085] 20230719 JasonKHLee >>
    bool exec_xcall_reply(Command::Ptr pCmd); // << [LE0-9412] 20240109 CK >>
    void exec_xcall_timeout(Command::Ptr pCmd); // << [LE0-9412] 20240109 CK >>
    void exec_mcu_timeout(Command::Ptr pCmd); // << [LE0-15085] 20240719 JasonKHLee >>
    void exec_audio_Cmd(Command::Ptr pCmd);
    void updateDiagXCallState(DIAG_CONDITION_XXCALL_STATE diagConditionXCallState); // << [LE0-9412] 20240109 CK >>
    // << [LE0WNC-9033] 20230217 CK
    void updateDiag_xcall_driving_State(DIAG_CONDITION_XCALL_DRIVING diagConditionXCALL_Driving_State); // << [LE0-15085] 20240717 JasonKHLee >>
    static std::string converToHex(std::string hexValue, size_t size);
    static std::string converToHex(std::string hexValue);
    // >> [LE0WNC-9033]
    static std::string decodeDMSmsNumberPayload(std::string payload); // << [LE0-5327] 20230606 CK >>

private:
    xCallManager * m_pxCallManager;
    CLogger& _logger;
	Poco::OSP::BundleContext::Ptr m_pContext;
	Poco::Thread _pThread;
	bool needstop{false};
    CxCallThreadsAliveWatcher::ThreadInfo* mThreadInfo; // << [LE0-5619] 20230615 CK >>
    CmdHandler CHD;
    bool mIsECallConnected{false}; // << [LE022-2567] 20240515 CK >>
    Poco::NotificationQueue waitECallConnectedCmdQueue; // << [LE022-2567] 20240515 CK >>

	//<< [LE0-7774] TracyTu
	static const uint8_t NRC22{0x22U};
    static const uint8_t NRC24{0x24U};
	//>> [LE0-7774]

	std::unordered_map<uint32_t, execFunction> handle_acall_cmd_function_map;
    void init_handle_aCall_cmd_function_map();

	void handle_aCall_cmd_XCALL_BCALL_BTN_PRESS();
	void handle_aCall_cmd_XCALL_BCALL_BTN_RELEASE();
	void handle_aCall_cmd_XCALL_EM_DEBUG_STATE(Command::Ptr pCmd);
	void handle_aCall_cmd_XCALL_EM_AWAKENING_NOTIFY(Command::Ptr pCmd);
	void handle_aCall_cmd_XCALL_BCALL_MANUAL_TRIGGER();
	void handle_aCall_cmd_XCALL_BCALL_BTN_LONG_PRESS();
	void handle_aCall_cmd_XCALL_BCALL_AUTO_TRIGGER();
	void handle_aCall_cmd_XCALL_BCALL_CALL_BACK();
	void handle_aCall_cmd_XCALL_BCALL_MANUAL_CANCEL();
	void handle_aCall_cmd_XCALL_BCALL_CALLOUT_SUCCESS();
	void handle_aCall_cmd_XCALL_BCALL_CALLOUT_FAIL();
	void handle_aCall_cmd_XCALL_BCALL_REGISTRATION_SUCCESS(Command::Ptr pCmd);
	void handle_aCall_cmd_XCALL_BCALL_MSD_TRANSFER_SUCCESS();
	void handle_aCall_cmd_XCALL_BCALL_MSD_REQUEST(Command::Ptr pCmd);
	void handle_aCall_cmd_XCALL_BCALL_BTN_SHORT_PRESS();
	void handle_aCall_cmd_XCALL_STATE_ECALL_WAITING_FOR_CALLBACK();
	void handle_aCall_cmd_XCALL_STATE_ECALL_VOICE_COMMUNICATION();
	void handle_aCall_cmd_XCALL_STATE_ECALL_TRIGGERED();
	void handle_aCall_cmd_XCALL_STATE_ECALL_OFF();
	void handle_aCall_cmd_XCALL_ECALL_ROUTINE_CTL_REQ(Command::Ptr pCmd);
	void handle_aCall_cmd_XCALL_ECALL_ROUTINE_CTL_ABORT(Command::Ptr pCmd); // << [LE0-7867][LE0-7868][LE0-7863] 20230906 CK >>
	void handle_aCall_cmd_XCALL_BACKUP_BATT_NOTIFY(Command::Ptr pCmd);
	void handle_aCall_cmd_XCALL_STATE_BCALL_CALLBACK_TIMEOUT();
	void handle_aCall_cmd_XCALL_STATE_ECALL_CALLBACK_TIMEOUT(Command::Ptr pCmd);
	void handle_aCall_cmd_XCALL_BCALL_DM_ACALL_SMS_NUMBER(Command::Ptr pCmd);
	void handle_aCall_cmd_XCALL_ECALL_BTN_event(Command::Ptr pCmd);
	void handle_aCall_cmd_XCALL_NEED_SYNC_BCALL_TO_ECALL();
	void handle_aCall_cmd_XCALL_BCALL_HTTP_MESSAGE_RESPONSE(Command::Ptr pCmd);
	void handle_aCall_cmd_XCALL_TPSECALL_SMS_REQUEST(Command::Ptr pCmd); //<< [LE0-11230]TracyTu, TPS ecall requirement >>
    void handle_aCall_cmd_XCALL_DIAG_STATE_UPDATE(Command::Ptr pCmd); // << [LE0-9412] 20240109 CK >>
    void handle_aCall_cmd_XCALL_DIAG_STATE_NEED_SYNC(Command::Ptr pCmd); // << [LE0-9412] 20240109 CK >>
    void handle_aCall_cmd_XCALL_DTC_CHANGE_NOTIFY(Command::Ptr pCmd); // << [LE0-11009][LE0-11039][LE022-983] 20240301 CK >>
    void handle_aCall_cmd_XCALL_ECALL_BTN_BLOCKED(Command::Ptr pCmd); // << [LE0-12307] 20240402 EasonCCLiao >>
    void handle_aCall_cmd_XCALL_BCALL_BTN_BLOCKED(Command::Ptr pCmd); // << [LE0-12307] 20240402 EasonCCLiao >>
    void handle_aCall_cmd_XCALL_DID_CHANGE_NOTIFY(Command::Ptr pCmd); // << [LE022-1667] TracyTu, Support one image >>
    void handle_aCall_cmd_XCALL_ACALL_ECALL_CONNECTED(Command::Ptr pCmd); // << [LE022-2567] 20240515 CK >>
    void handle_aCall_cmd_XCALL_ACALL_ECALL_DISCONNECTED(Command::Ptr pCmd); // << [LE022-2567] 20240515 CK >>
    void handle_aCall_cmd_XCALL_BCALL_RECOVERY_CALL_CRASH(Command::Ptr pCmd); // << [LE022-2739] 20240528 CK >>
    void handle_aCall_cmd_XCALL_BCALL_RECOVERY_PRESS_CRASH(Command::Ptr pCmd); // << [LE022-2739] 20240528 CK >>
    //<< [LE0-11230]TracyTu, TPS ecall requirement 
    void handle_aCall_cmd_XCALL_TPSECALL_DM_ECALL_SMS_NUMBER(Command::Ptr pCmd);
    //>> [LE0-11230]
    void handle_aCall_cmd_XCALL_DRIVING_DIAG_STATE_UPDATE(Command::Ptr pCmd); // << [LE0-15085] JasonKHLee
    void handle_aCall_cmd_XCALL_DRIVING_DIAG_STATE_NEED_SYNC(Command::Ptr pCmd); // << [LE0-15085] JasonKHLee
    void handle_aCall_cmd_XCALL_ECALL_URC_DEMOAPP_STATUS_IND(Command::Ptr pCmd); // << [LE022-3863] 20240730 ZelosZSCao >>
    void syncDiagXCallStateStopAndWaitArq(); // << [LE0-9412] 20240109 CK >>
    void syncDiagXCallDrivingStopAndWaitArq(); // << [LE0-15085] 20240719 JasonKHLee >>
    bool receiveAckDiagXCallStateStopAndWaitArq(Command::Ptr pCmd); // << [LE0-9412] 20240109 CK >>
    bool receiveAckDiagXCallDrivingStopAndWaitArq(Command::Ptr pCmd); // << [LE0-15085] 20240719 JasonKHLee >>
    void timeoutDiagXCallStateStopAndWaitArq(); // << [LE0-9412] 20240109 CK >>
    void timeoutDiagXCallDrivingStopAndWaitArq();
	std::string build_routine_ctl_rsp(std::string in_packet, bool is_handled_ok, uint8_t routine_status, uint8_t nrcCode = NRC22); //<< [LE0-7774] TracyTu >>
    bool can_be_exec(uint32_t cmdId); // << [LE022-1667] TracyTu, Support one image >>
    bool checkNeedCoverNEENotReadySituation(Command::Ptr pCmd); // << [LE022-2567] 20240515 CK >>
    bool checkNeedCoverNEENotReadySituationACall(Command::Ptr pCmd, const bool isTriggerCallCmd); // << [LE022-2739][LE022-2679] 20240522 ck >>
    bool checkNeedCoverNEENotReadySituationECall(const bool isTriggerCallCmd); // << [LE022-2739][LE022-2679] 20240522 ck >>

	bool xCallDrivingResult{false}; //<<[LE0-7328] yusanhsu>>
    bool isNeedToResponseForXCallDriving{false}; //<<[LE0-7328] yusanhsu>>

    XCallStateStopAndWaitARQ xCallStateArqIn = {0xff, XXCALL_STATE_OFF}; // << [LE0-9412] 20240109 CK >>
    XCallStateStopAndWaitARQ xCallStateArqOut = {0, XXCALL_STATE_OFF}; // << [LE0-9412] 20240109 CK >>
    XCallStateStopAndWaitARQ xCallStateArqCurrent = {0, XXCALL_STATE_OFF}; // << [LE0-9412] 20240109 CK >>
    XCallDrivingStopAndWaitARQ xCallDrivingArqIn = {0xff, XCALL_DRIVING_NOT_READY}; // << [LE0-15085] 20240719 JasonKHLee >>
    XCallDrivingStopAndWaitARQ xCallDrivingArqOut = {0xff, XCALL_DRIVING_NOT_READY}; // << [LE0-15085] 20240719 JasonKHLee >>
    XCallDrivingStopAndWaitARQ xCallDrivingArqCurrent = {0xff, XCALL_DRIVING_NOT_READY}; // << [LE0-15085] 20240719 JasonKHLee >>
    const std::string DID_2103_TEL_FCT_XCALL{"8451"};  //0x2103 // << [LE022-1667] TracyTu, Support one image >>
    //<< [LE022-4538] TracyTu, xCall trace develop 
    const uint16_t BBS_XCALL_EVENT{0x2200U};
    const uint8_t BBS_EVENT_ACALL_BTN_BLOCK{0U};
    const uint8_t BBS_EVENT_ECALL_BTN_BLOCK{1U};
    //>> [LE022-4538] 
};

}  // namespace MD::xCallManage
#endif
