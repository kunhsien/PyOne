#ifndef CECALLEVENTLOG_H_
#define CECALLEVENTLOG_H_
#include "Poco/Logger.h"
#include "Poco/BasicEvent.h"

#include "DataStore.h"
#include <any>
#include "Poco/JSON/Object.h"
#include "Poco/JSON/Array.h"
#include "Poco/FileStream.h"
#include "Poco/File.h"
#include "Poco/Util/Application.h"
#include "Poco/StreamCopier.h"
#include "Poco/LocalDateTime.h"
#include "Poco/DateTimeFormat.h"
#include "Poco/DateTimeFormatter.h"
#include "Poco/Delegate.h"
#include "Poco/JSON/Parser.h"
#include "Poco/Dynamic/Var.h"
#include "Poco/Format.h"
#include "Poco/DirectoryIterator.h"
//LouisLee 2024/4/14 eap_xxx be wnc header ,remove
//#include "eap_errno.h"
//#include "eap_api_r.h"
//#include "eap_common.h"
//LouisLee 2024/4/14 eap_xxx be wnc header ,remove
#include "nad_tel.h"
#include "Poco/Timer.h"
#include <map>
#include "CeCallMachine.h"
#include "CLogger.h"

namespace MD {
namespace eCallMgr {

class eCallManager;
typedef enum {
    CAN_INFO_CRASH_CONTENT = 0,
    CELLULAR_NETWORK_STATE = 1,
    CELLULAR_NETWORK_CELL = 2,
    ECALL_SESSION_STATE = 3,
    GNSS_FIX_FIX = 4,
    LOGFILE_SAVING_ATTEMPT_STATE = 5,
    MSD_TRANSMISSION_STATE = 6,
    PSAP_CALL_STATE = 7,
    TCU_POWER = 8,
    VEHICLE_SEV = 9,
    VEHICLE_CAN = 10,
	FILE_EXPIRED_TIME_OUT = 11,
	CONFIG_PARAM_READY = 12
}E_ECALL_EVENT;

typedef enum {
    ECALL_SESSION_STATE_STARTED = 0,
    ECALL_SESSION_STATE_WAIT = 1,
    ECALL_SESSION_STATE_STOPPED_OK = 2,
    ECALL_SESSION_STATE_STOPPED_NEW_ECALL = 3,
    ECALL_SESSION_STATE_STOPPED_ABORTED = 4,
    ECALL_SESSION_STATE_STOPPED_OTHER = 5,
    ECALL_SESSION_STATE_OTHER = 6
}E_ECALL_SESSION_STATE;

typedef enum {
    VEHICLE_CANSTATE_SLEEP = 0,
    VEHICLE_CANSTATE_WAKEUP = 1,
    VEHICLE_CANSTATE_NORMAL = 2,
    VEHICLE_CANSTATE_GOTOSLEEP = 3,
    VEHICLE_CANSTATE_COMEOFF = 4
}E_VEHICLE_CANSTATE;

typedef enum {
    ECALL_SESSION_INIT_ECALL_TRIGGER_AUTO = 0,
    ECALL_SESSION_INIT_ECALL_TRIGGER_MANUAL = 1
}E_ECALL_SESSION_INIT_ECALL_TRIGGER;

typedef enum {
    E_SESSION_STATE_STOP = 0,
    E_SESSION_STATE_START = 1,
}E_SESSION_STATE;

typedef enum {
	TCU_POWER_VEHICLE = 0,
	TCU_POWER_BACKUP = 1
}E_TCU_POWER;

typedef enum {
    E_LOG_FILE_EXPIRED_TIME_OUT = 0,
	E_LOG_FILE_EXPIRED_ONE_SHOT_TIME_OUT = 1
}E_TimeOut_REASON;  

typedef std::map<uint32_t, std::string> elem_info_data_map;

const std::string Enum_CANinfo_crash_content[]{"0000", "0001", "0010", "0011"};
const std::string Enum_Registration_state[]{"UNREGISTERED", "REGISTERED_HOME", "SEARCHING", "DENIED", "UNKNOWN", "REGISTERED_ROAMING"};
const std::string Enum_eCall_session_state[]{"STARTED", "WAIT", "STOPPED_OK", "STOPPED_NEW_ECALL", "STOPPED_ABORTED", "STOPPED_OTHER", "OTHER"};

//Vehicle info
const std::string Enum_Vehicle_sev[]{"STOP", "CONTACT", "DEM", "FREE"};
const std::string Enum_Vehicle_canState[]{"SLEEP", "WAKEUP", "NORMAL", "GOTOSLEEP", "COMEOFF"};

const elem_info_data_map Enum_Vehicle_configMode = {{0, "FITTING"},{1, "PLANT"},{2, "CHECK"},{3, "STORAGE"}, {3, "TRANSPORT"},{4, "CLIENT"}, 
                                                    {5, "SHOWROOM"}, {6, "APV"},{15, "INVALID"}};

const std::string Enum_eCall_session_init_ecall_trigger[]{"AUTO", "MANUAL"};

const std::string Enum_PASP_call_state[]{"NONE", "FAILED", "HANGUP", "PICKUP", "INCOMING", "OUTGOING", "MUTE", "UNMUTE"};
const std::string Enum_MSD_trans_state[]{"SENDING", "ACKNOWLEDGED", "REQUESTED", "OTHER"};
const std::string Enum_eCall_session_mode[]{"NONE", "FIH_FAST_ECALL_TEST", "FIH_FAST_ECALL_EMERGENCY", "FIH_FAST_ECALL_RECONFIG"};
const std::string Enum_TCU_power[]{"VEH", "BACKUP_BATTERY"};

const std::string invalid_str{"INVLAID_FORMAT"};  

const long EVENT_LOG_PEIROD_INTERVAL{60*1000 };  //1min
const long EVENT_LOG_DEL_EXPIRED_NOT_ALLOW{60};   //Delay 1min to delete expired log file, when the ecall session was not stopped. //<< [LE0-9065] TracyTu >>
#define NUM_OF_ELEMENTS(array)  (sizeof(array) / sizeof(array[0]))

class CeCallEventLog
{
private:
	typedef struct ecall_Logfile_properties{
		private:
			int32_t id{0};
			std::string usage;
			std::string url;
			int32_t retain;
			std::string version;
		public:
			int32_t getId() const { return id; }			
			std::string getUsage() const { return usage; }			
			std::string getUrl() const { return url; }			
			int32_t getRetain() const { return retain; }
			std::string getVersion() const {return version; }

			void setId(const int32_t value) { id = value; }
			void setUsage(const std::string value) { usage = value; }
			void setUrl(const std::string value) { url = value; }
			void setRetain(const int32_t value) { retain = value; }
			void setVersion(const std::string value) { version = value; }
	}sEcall_Logfile_properties;

	typedef struct ecall_Logfile_CANinfo_crash{
		private:
			int64_t time;
			std::string type;
			std::string content{""};
		public:
			int64_t getTime() const { return time; }
			std::string getType() const { return type; }
			std::string getContent() const { return content; }

			void setTime(const int64_t value) { time = value; }
			void setType(const std::string value) { type = value; }
			void setContent(const std::string value) { content = value; }
	}sEcall_Logfile_CANinfo_crash;

	typedef struct ecall_Cellular_network_Cell{
		private:
			int32_t id;
			std::string name;
			int32_t mcc;
			int32_t mnc;
			std::string rat;
			int32_t rxlevel;
		public:
			int32_t getId() const { return id; }
			std::string getName() const { return name; }
			int32_t getMcc() const { return mcc; }
			int32_t getMnc() const { return mnc; }
			std::string getRat() const { return rat; }
			int32_t getRxlevel() const { return rxlevel; }

			void setId(const int32_t value) { id = value; }
			void setName(const std::string value) { name = value; }
			void setMcc(const int32_t value) { mcc = value; }
			void setMnc(const int32_t value) { mnc = value; }
			void setRat(const std::string value) { rat = value; }
			void setRxlevel(const int32_t value) { rxlevel = value; }
	}sEcall_Cellular_network_Cell;

	typedef struct ecall_Cellular_network_GSM_signal{
		private:
			int32_t ss;
			int32_t ber;
		public:
			int32_t getSs() const { return ss; }
			int32_t getBer() const { return ber; }

			void setSs(const int32_t value) { ss = value; }
			void setBer(const int32_t value) { ber = value; }
	}sEcall_Cellular_network_GSM_signal;

	typedef struct ecall_Cellular_network_UMTS_signal{
		private:
			int32_t ss;
			int32_t ber;
			int32_t ecio;
			int32_t rscp;
			int32_t sinr;
		public:
		    int32_t getSs() const { return ss; }
			int32_t getBer() const { return ber; }
			int32_t getEcio() const { return ecio; }
			int32_t getRscp() const { return rscp; }
			int32_t getSinr() const { return sinr; }

			void setSs(const int32_t value) { ss = value; }
			void setBer(const int32_t value) { ber = value; }
			void setEcio(const int32_t value) { ecio = value; }
			void setRscp(const int32_t value) { rscp = value; }
			void setSinr(const int32_t value) { sinr = value; }
	}sEcall_Cellular_network_UMTS_signal;

	typedef struct ecall_Cellular_network_LTE_signal{
		private:
			int32_t ss;
			int32_t ber;
			int32_t rsrq;
			int32_t rsrp;
			int32_t snr;
		public:
			int32_t getSs() const { return ss; }
			int32_t getBer() const { return ber; }
			int32_t getRsrq() const { return rsrq; }
			int32_t getRsrp() const { return rsrp; }
			int32_t getSnr() const { return snr; }

			void setSs(const int32_t value) { ss = value; }
			void setBer(const int32_t value) { ber = value; }
			void setRsrq(const int32_t value) { rsrq = value; }
			void setRsrp(const int32_t value) { rsrp = value; }
			void setSnr(const int32_t value) { snr = value; }
	}sEcall_Cellular_network_LTE_signal;

	typedef struct ecall_Cellular_network_CDMA_signal{
		private:
			int32_t ss;
			int32_t ber;
			int32_t ecio;
			int32_t sinr;
			int32_t io;
		public:
		    int32_t getSs() const { return ss; }
			int32_t getBer() const { return ber; }
			int32_t getEcio() const { return ecio; }
			int32_t getSinr() const { return sinr; }
			int32_t getIo() const { return io; }

			void setSs(const int32_t value) { ss = value; }
			void setBer(const int32_t value) { ber = value; }
			void setEcio(const int32_t value) { ecio = value; }
			void setSinr(const int32_t value) { sinr = value; }
			void setIo(const int32_t value) { io = value; }
	}sEcall_Cellular_network_CDMA_signal;

	typedef union ecall_Cellular_network_signal{
		sEcall_Cellular_network_GSM_signal GSM_signal;
		sEcall_Cellular_network_UMTS_signal UMTS_signal;
		sEcall_Cellular_network_LTE_signal LTE_signal;
		sEcall_Cellular_network_CDMA_signal CDMA_signal;
	}uEcall_Cellular_network_signal;

	typedef struct ecall_Logfile_Cellular_network{
		private:
			int64_t time{};
			std::string type{};
			std::string state{};
			sEcall_Cellular_network_Cell cell{};
			uEcall_Cellular_network_signal signal{};
			std::string data{};
		public:
			int64_t getTime() const { return time; }
			std::string getType() const { return type; }
			std::string getState() const { return state; }
			sEcall_Cellular_network_Cell& getCell() { return cell; }
			uEcall_Cellular_network_signal& getSignal() { return signal; }
			std::string getData() const { return data; }
 
			void setTime(const int64_t value) { time = value; }
			void setType(const std::string value) { type = value; }
			void setState(const std::string value) { state = value; }
			void setCell(const sEcall_Cellular_network_Cell& value) { cell = value; }
			void setSignal(const uEcall_Cellular_network_signal& value) { signal = value; }
			void setData(const std::string value) { data = value; }
			bool isSameCell(const sEcall_Cellular_network_Cell& value)
			{
				return (cell.getRat() == value.getRat()) && (cell.getMcc() == value.getMcc()) && (cell.getMnc() == value.getMnc()) && (cell.getId() == value.getId());
			}
	}sEcall_Logfile_Cellular_network;

	typedef struct ecall_eCall_session_Cellular_network_init{
		private:
			std::string state;
			sEcall_Cellular_network_Cell cell;
			uEcall_Cellular_network_signal signal;
			sEcall_Cellular_network_Cell cellOthers;
		public:
			std::string getState() const { return state; }
			sEcall_Cellular_network_Cell& getCell() { return cell; }
			uEcall_Cellular_network_signal& getSignal() { return signal; }
			sEcall_Cellular_network_Cell& getCellOthers() { return cellOthers; }

			void setState(const std::string value) { state = value; }
			void setCell(const sEcall_Cellular_network_Cell& value) { cell = value; }
			void setSignal(const uEcall_Cellular_network_signal& value) {  signal = value; }
			void setCellOthers(const sEcall_Cellular_network_Cell& value) { cellOthers = value; }
	}sEcall_eCall_session_Cellular_network_init;

	typedef struct ecall_GNSS_fix_Dilution_of_precision{
		uint8_t p;
		uint8_t h;
		uint8_t v;
	}sEcall_GNSS_fix_Dilution_of_precision;

	typedef struct ecall_GNSS_fix_Satellite {
		std::string snr;
	}sEcall_GNSS_fix_Satellite;

	typedef struct ecall_GNSS_fix_Position_error {
		uint8_t hErr;
	}sEcall_GNSS_fix_Position_error;

	typedef struct ecall_GNSS_fix_GNSS{
		private:
			std::string fix;
			std::string system;
			sEcall_GNSS_fix_Dilution_of_precision dop;
			sEcall_GNSS_fix_Satellite sat;
			sEcall_GNSS_fix_Position_error err;
		public:
			std::string getFix() const { return fix; }
			std::string getSystem() const { return system; }
			sEcall_GNSS_fix_Dilution_of_precision& getDop() {return dop; }
			sEcall_GNSS_fix_Satellite& getSat() {return sat; }
			sEcall_GNSS_fix_Position_error& getErr() { return err; }

			void setFix(const std::string value) { fix = value; }
			void setSystem(const std::string value) { system = value; }
			void setDop(const sEcall_GNSS_fix_Dilution_of_precision& value) { dop = value; }
			void setSat(const sEcall_GNSS_fix_Satellite& value) { sat = value; }
			void setErr(const sEcall_GNSS_fix_Position_error& value) { err = value; }
	}sEcall_GNSS_fix_GNSS;

	typedef struct ecall_Logfile_GNSS_fix{
		private:
			int64_t time;
			std::string type;
			std::string fix;
			std::string system;
			sEcall_GNSS_fix_Dilution_of_precision dop;
			sEcall_GNSS_fix_Satellite sat;
			sEcall_GNSS_fix_Position_error err;
		public:
			int64_t getTime() const { return time; }
			std::string getType() const { return type; }
			std::string getFix() const { return fix; }
			std::string getSystem() const { return system; }
			sEcall_GNSS_fix_Dilution_of_precision& getDop() { return dop; }
			sEcall_GNSS_fix_Satellite& getSat() { return sat; }
			sEcall_GNSS_fix_Position_error& getErr() { return err; }	

			void setTime(const int64_t value) { time = value; }
			void setType(const std::string value) { type = value; }
			void setFix(const std::string value) { fix = value; }
			void setSystem(const std::string value) { system = value; }
			void setDop(const sEcall_GNSS_fix_Dilution_of_precision& value) { dop = value; }
			void setSat(const sEcall_GNSS_fix_Satellite& value) { sat = value; }
			void setErr(const sEcall_GNSS_fix_Position_error& value) { err = value; }
			bool isSameFix(const std::string value) { return (fix == value); }
	}sEcall_Logfile_GNSS_fix;

	typedef struct ecall_eCall_session_eCall_init{
		private:
			std::string trigger{};
			std::string mode{};
			std::string psapNumber{};
			uint8_t infoCrashFrame{};
		public:
			std::string getTrigger() const { return trigger; }
			std::string getMode() const { return mode; }
			std::string getPsapNumber() const { return psapNumber; }
			uint8_t getInfoCrashFrame() const { return infoCrashFrame; }			
		
		    void setTrigger(const std::string value) { trigger = value; } 
			void setMode(const std::string value) { mode = value; }
			void setPsapNumber(const std::string value) { psapNumber = value; }
			void setInfoCrashFrame(const uint8_t value) { infoCrashFrame = value; }
	}sEcall_eCall_session_eCall_init;

	typedef struct ecall_eCall_session_GNSS_init{
		private:
			std::string fix;
			std::string system;
			sEcall_GNSS_fix_Dilution_of_precision dop;
			sEcall_GNSS_fix_Satellite sat;
			sEcall_GNSS_fix_Position_error err;
		public:
			std::string getFix() const { return fix; }
			std::string getSystem() const { return system; }
			sEcall_GNSS_fix_Dilution_of_precision& getDop() { return dop; }
			sEcall_GNSS_fix_Satellite& getSat() { return sat; }
			sEcall_GNSS_fix_Position_error& getErr() { return err; }		

			void setFix(const std::string value) { fix = value; }
			void setSystem(const std::string value) { system = value; }
			void setDop(const sEcall_GNSS_fix_Dilution_of_precision& value) { dop = value; }
			void setSat(const sEcall_GNSS_fix_Satellite& value) { sat = value; }
			void setErr(const sEcall_GNSS_fix_Position_error& value) { err = value; }
	}sEcall_eCall_session_GNSS_init;

	typedef struct ecall_eCall_session_TCU_init{
		private:
			std::string imei;
			std::string power;
			std::string hwRef;
			std::string swVersionTel;
			std::string swEditionTel;
			std::string swFile;
			std::string rxSwin;
		public:
			std::string getImei() const { return imei; }
			std::string getPower() const { return power; }
			std::string getHwRef() const { return hwRef; }
			std::string getSwVersionTel() const { return swVersionTel; }
			std::string getSwEditionTel() const { return swEditionTel; }
			std::string getSwFile() const { return swFile; }
			std::string getRxSwin() const { return rxSwin; }	

			void setImei(const std::string value)	{ imei= value; }
			void setPower(const std::string value)	{ power= value; }
			void setHwRef(const std::string value)	{ hwRef= value; }
			void setSwVersionTel(const std::string value)	{ swVersionTel= value; }
			void setSwEditionTel(const std::string value)	{ swEditionTel= value; }
			void setSwFile(const std::string value)	{ swFile= value; }
			void setRxSwin(const std::string value)	{ rxSwin= value; }
	}sEcall_eCall_session_TCU_init;

	typedef struct ecall_eCall_session_Vehicle_init{
		private:
			std::string canState;
			std::string sev;
			std::string config;
		public:
			std::string getCanState() const { return canState; }
			std::string getSev() const { return sev; }
			std::string getConfig() const { return config; }

			void setCanState(const std::string value)	{ canState= value; }
			void setSev(const std::string value)	{ sev= value; }
			void setConfig(const std::string value)	{ config= value; }
	}sEcall_eCall_session_Vehicle_init;

	typedef struct ecall_eCall_session_init{
		private:
			sEcall_eCall_session_Cellular_network_init cellnet;
			sEcall_eCall_session_eCall_init ecall;
			sEcall_eCall_session_GNSS_init gnss;
			sEcall_eCall_session_TCU_init tcu;
			sEcall_eCall_session_Vehicle_init veh;
		public:
			sEcall_eCall_session_Cellular_network_init& getCellnet() { return cellnet; }
			sEcall_eCall_session_eCall_init& getEcall() { return ecall; }
			sEcall_eCall_session_GNSS_init& getGnss() { return gnss; }
			sEcall_eCall_session_TCU_init& getTcu() { return tcu; }
			sEcall_eCall_session_Vehicle_init& getVeh() { return veh; }	

			void setCellnet(const sEcall_eCall_session_Cellular_network_init& value) { cellnet = value; }
			void setEcall(const sEcall_eCall_session_eCall_init& value) { ecall = value; }
			void setEcall(const sEcall_eCall_session_GNSS_init& value) { gnss = value; }
			void setEcall(const sEcall_eCall_session_TCU_init& value) { tcu = value; }
			void setEcall(const sEcall_eCall_session_Vehicle_init& value) { veh = value; }
	}sEcall_eCall_session_init;

	typedef struct ecall_Logfile_eCall_session{
		private:
			int64_t time;
			std::string type;
			std::string state;
			int32_t odometer;
			int32_t tcuTemp;
			sEcall_eCall_session_init init;
			std::string data;
		public:
			int64_t getTime() const { return time; }
			std::string getType() const { return type; }
			std::string getState() const { return state; }
			int32_t getOdometer() const { return odometer; }
			int32_t getTcuTemp() const { return tcuTemp; }
			sEcall_eCall_session_init& getInit() { return init; }
			std::string getData() const { return data; }

			void setTime(const int64_t value) { time =  value; }
			void setType(const std::string value) { type = value; }
			void setState(const std::string value) { state = value; }
			void setOdometer(const int32_t value) { odometer = value; }
			void setTcuTemp(const int32_t value) { tcuTemp = value; }
			void setInit(const sEcall_eCall_session_init& value) { init = value; }
			void setData(const std::string value) { data = value; }
	}sEcall_Logfile_eCall_session;

	typedef struct ecall_Logfile_Logfile_saving_attempt{
		private:
			int64_t time;
			std::string type;
			std::string state;
			std::string httpStatusCode;
		public:
			int64_t getTime() const { return time; }
			std::string getType() const { return type; }
			std::string getState() const { return state; }
			std::string getHttpStatusCode() const { return httpStatusCode; }

			void setTime(const int64_t value) { time = value; }
			void setType(const std::string value) { type = value; }
			void setState(const std::string value) { state = value; }
			void setHttpStatusCode(const std::string value) { httpStatusCode = value; }
	}sEcall_Logfile_Logfile_saving_attempt;

	typedef struct ecall_Logfile_MSD_transmission{
		private:
			int64_t time;
			std::string type;
			std::string state;
			std::string source;
			sEcall_GNSS_fix_GNSS gnss;
			std::string data;
		public:
			int64_t getTime() const { return time; }
			std::string getType() const { return type; }
			std::string getState() const { return state; }
			std::string getSource() const { return source; }
			sEcall_GNSS_fix_GNSS& getGnss() { return gnss; }
			std::string getData() const { return data; }

			void setTime(const int64_t value) { time = value; }
			void setType(const std::string value) { type = value; }
			void setState(const std::string value) { state = value; }
			void setSource(const std::string value) { source = value; }
			void setGnss(const sEcall_GNSS_fix_GNSS& value) { gnss = value; }
			void setData(const std::string value) { data = value; }
	}sEcall_Logfile_MSD_transmission;

	typedef struct ecall_Logfile_PSAP_call{
		private:
			int64_t time;
			std::string type;
			std::string state;
			std::string data;
		public:
			int64_t getTime() const { return time; }
			std::string getType() const { return type; }
			std::string getState() const { return state; }
			std::string getData() const { return data; }

			void setTime(const int64_t value) { time = value; }
			void setType(const std::string value) { type = value; }
			void setState(const std::string value) { state = value; }
			void setData(const std::string value) { data = value; }		
	}sEcall_Logfile_PSAP_call;

	typedef struct ecall_Logfile_TCU{
		private:
			int64_t time;
			std::string type;
			std::string power;
		public:
			int64_t getTime() const { return time; }
			std::string getType() const { return type; }
			std::string getPower() const { return power; }

			void setTime(const int64_t value) { time = value; }
			void setType(const std::string value) { type = value; }
			void setPower(const std::string value) { power = value; }
	}sEcall_Logfile_TCU;

	typedef struct ecall_Logfile_Vehicle{
		private:
			int64_t time;
			std::string type;
			std::string sev;
			std::string canState;
		public:
			int64_t getTime() const { return time; }
			std::string getType() const { return type; }
			std::string getSev() const { return sev; }
			std::string getCanState() const { return canState; }

			void setTime(const int64_t value) { time = value; }
			void setType(const std::string value) { type = value; }
			void setSev(const std::string value) { sev = value; }
			void setCanState(const std::string value) { canState = value; }		
	}sEcall_Logfile_Vehicle;

	typedef struct ecall_Logfile{
		private:
			sEcall_Logfile_properties prop;
			sEcall_Logfile_CANinfo_crash CANinfo_crash_log;
			sEcall_Logfile_Cellular_network Cellular_network_log;
			sEcall_Logfile_eCall_session eCall_session_log;
			sEcall_Logfile_GNSS_fix GNSS_fix_log;
			sEcall_Logfile_Logfile_saving_attempt Logfile_saving_attempt_log;
			sEcall_Logfile_MSD_transmission MSD_transmission_log;
			sEcall_Logfile_PSAP_call PSAP_call_log;
			sEcall_Logfile_TCU TCU_log;
			sEcall_Logfile_Vehicle Vehicle_log;
	    public:
			sEcall_Logfile_properties& getProp() { return prop; }
			sEcall_Logfile_CANinfo_crash& getCANinfoCrashLog() { return CANinfo_crash_log; }
			sEcall_Logfile_Cellular_network& getCellularNetworkLog() { return Cellular_network_log; }
			sEcall_Logfile_eCall_session& getEcallSessionLog() { return eCall_session_log; }
			sEcall_Logfile_GNSS_fix& getGNSSFixLog() { return GNSS_fix_log; }
			sEcall_Logfile_Logfile_saving_attempt& getLogfileSavingAttemptLog() { return Logfile_saving_attempt_log; }
			sEcall_Logfile_MSD_transmission& getMSDTransmissionLog() { return MSD_transmission_log; }
			sEcall_Logfile_PSAP_call& getPSAPCallLog() { return PSAP_call_log; }
			sEcall_Logfile_TCU& getTCULog() { return TCU_log; }
			sEcall_Logfile_Vehicle& getVehicleLog() { return Vehicle_log; }	
	}sEcall_Logfile;

	typedef struct eCall_LogFileCtx
	{
		std::string   m_name;
		Poco::LocalDateTime m_time;
	} sEcall_LogFileCtx;

public:
    CeCallEventLog(eCallManager* const pMgr);
    virtual ~CeCallEventLog();
    virtual void stopTimer();
	virtual void notifyUpdateEvent(const int32_t event, const uint32_t param_0);
	virtual void notifyUpdateEvent(const int32_t event, const uint32_t param_0, const uint32_t param_1);
	virtual void notifyUpdateEvent(const int32_t event, const uint32_t param_0, const uint32_t param_1, const uint32_t param_2);
	virtual void notifyUpdateEvent(const int32_t event, const int32_t param_0, const uint32_t param_1, const std::string param_2);

	void updateEventLog(const sEcall_Logfile_eCall_session& ecallSession);
	void updateEventLog(const sEcall_Logfile_CANinfo_crash& canCrashInfo);
	void updateEventLog(sEcall_Logfile_Cellular_network& cellNetwork);
	void updateEventLog(sEcall_Logfile_GNSS_fix& gnss);
	void updateEventLog(const sEcall_Logfile_PSAP_call& psapCall);
	void updateEventLog(const sEcall_Logfile_MSD_transmission& msdTrans);
	void updateEventLog(const sEcall_Logfile_Vehicle& vehicle);
	void updateEventLog(const sEcall_Logfile_TCU& tcu);
    void setGnssFix(const std::string fix) { gnss_test_fix = fix; }

    sEcall_Logfile getEventLog()const { return m_sEventLog; }
	uint32_t getFileNumber()const {return m_fileNumber; }
	void setFileNumber(const uint32_t fileNumber) {m_fileNumber = fileNumber; }
	uint32_t getSessionId()const { return m_sessionId; }
	void setSessionId(const uint32_t sessionId) { m_sessionId = sessionId; }
	NAD_RIL_Errno get_cell_info_list(nad_network_cellinfo_list_t &cell_info_list); //<< [LE0-10138] TracyTu, eCall trace develop >>
    std::string get_vehicle_config_mode(); // << [LE0-10901] 20240223 ck >>

private:
	void onUpdateEventLog(std::string& updateEvent);

	const std::string toString(const std::vector<uint16_t> data_set);
    const std::string toString(const uint32_t index, const std::string table[], uint32_t numOfElemts);
	const std::string toString(const uint32_t data, const bool is_hex);
	void initEventObjects();
	void initLogFile();
	void initEventObjectsForNewSession();
	void createInitialEventLog();
	void store(Poco::JSON::Object::Ptr event);
	void startSession();
    void endSession();
    bool isSessionStart()const
	{
		return m_SessionState == E_SESSION_STATE_START; 
	} 
	void collect_event(Poco::JSON::Object::Ptr event);
	void update();
	uint32_t get_vehicle_odometer();
	void get_Identification_Zone_for_Downloadable_ECU_service(DATA_IdentificationZone& data);
	std::string get_product_number();
	std::string get_regulation_xswids_num();
	int64_t get_unix_time();
	uint8_t get_info_crash();
	bool isVehPower()const
	{
		return m_TcuPower == TCU_POWER_VEHICLE; 
	}
	std::string get_International_Mobile_Equipment_Identity();
	std::string get_Name_of_the_operator();
	NAD_RIL_Errno getCellNetworkCellData(sEcall_Logfile_Cellular_network& cellnet);
	void getGnssData(sEcall_Logfile_GNSS_fix& gnss);
	void getGnssInitData(sEcall_eCall_session_GNSS_init& gnss);
	void updateCellData(const bool toBeUpdatedEventLog);
	void updateGnssData();
	void TimeOut(Poco::Timer&);
	void logFileExpiredTimerCallBack(Poco::Timer&);
	void handleLogFileExpiredTimeOut();
	void handleTimeOut(E_TimeOut_REASON reason);
	void startLogFileExpired(const bool isCalledInTimeOutCallBack = false);
    void monitorLogFileExpired();
	bool isAllowedToRemove(const std::string& path); //<< [LE0-9065] TracyTu >>
	void delExpiredLogFile();
	void addLogFileElement(std::string& fileName);
	void removeLogFileElement(std::string& fileName);
	sEcall_LogFileCtx generateLogFileCtx(std::string& fileName);
	void buildLogFiles();
	long MAXIMUM_EVENT_LOG_FILE_VALIDTY_DURATION_S()const;
	long micro_to_second(const long Ts) { return Ts/1000000; }
    long milli_to_microsecond(const long Ts) { return Ts*1000; }
	long micro_to_millisecond(const long Ts) { return Ts/1000; } 
	long second_to_millisecond(const long Ts) { return Ts*1000; }
	long second_to_microsecond(const long Ts) { return Ts*1000*1000; }
	// << [LE0-15172] TracyTu, do update cell and gnss data in call session start period
	void startPeriodTimer(); 
	void stopPeriodTimer(); 
	// >> [LE0-15172] 

	eCallManager* const pecallMgr;
	CLogger& _logger;
	sEcall_Logfile m_sEventLog;
	Poco::BasicEvent <std::string> updateEvent;
	std::string m_folderPath;
	std::string m_fileName;
	uint32_t m_fileNumber{0U};
	E_SESSION_STATE m_SessionState {E_SESSION_STATE_STOP};
	E_TCU_POWER m_TcuPower {TCU_POWER_VEHICLE};
	uint32_t m_sessionId = 0U;
	std::vector<std::string> dynamicEvents {};
	Poco::Timer m_informTimer;
	std::string gnss_test_fix = ""; 
	Poco::Timer m_logFileExpiredTimer;
	std::vector<sEcall_LogFileCtx> m_logFileCtxs;  
	const uint8_t ECALL_EVENT_FILE_NUMBER{5U};
	const std::string EVENTLOG_FOLDER_PATH{"xCallLog/event"}; //LE0WNC-9813

	void updateCanInfoCrashContent(const uint32_t);
	void updateCellularNetworkState(const uint32_t, const uint32_t);
	void updateECallSessionStateStarted(const uint32_t, const std::string);
	void updateMsdTransmissionState(const uint32_t);
	void updatePSAPCallState(const uint32_t);
	void updateTCUPower(const uint32_t);
	void updateVEHICLE_SEV(const uint32_t);
	void updateVEHICLE_CAN(const uint32_t);

    struct {
        bool operator()(auto& lhs, auto& rhs) const {
            return lhs.m_time.timestamp() <= rhs.m_time.timestamp();
        }
    } CompareFunc;
};

} } // namespace MD::eCallMgr
#endif
