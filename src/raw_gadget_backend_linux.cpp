#include "usb_research/raw_gadget_backend.hpp"
#include "usb_research/trace.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <exception>
#include <fstream>
#include <iostream>
#include <atomic>
#include <thread>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>
#ifdef __linux__
#include <pthread.h>
#include <fcntl.h>
#include <linux/usb/ch9.h>
#include <linux/usb/raw_gadget.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace usb_research {
#ifdef __linux__
namespace {
volatile std::sig_atomic_t stop_requested=0;
void on_signal(int){stop_requested=1;}
void on_worker_interrupt(int) {}
std::uint16_t le16(const __le16& x) { const auto* p=reinterpret_cast<const unsigned char*>(&x);return static_cast<std::uint16_t>(p[0]|(p[1]<<8)); }
void check_ioctl(int fd,unsigned long req,void* arg,const std::string& name){if(ioctl(fd,req,arg)<0)throw std::runtime_error(name+" failed: "+std::strerror(errno));}
std::vector<std::uint8_t> transact_ep0(int fd, bool in, const std::vector<std::uint8_t>& data,
                                      std::uint16_t wanted, TraceLogger& trace, const Json& request) {
    const std::size_t n = in ? std::min<std::size_t>(data.size(), wanted) : wanted;
    std::vector<std::uint8_t> mem(sizeof(usb_raw_ep_io) + std::max<std::size_t>(n, 1));
    auto* io = reinterpret_cast<usb_raw_ep_io*>(mem.data());
    io->ep = 0;
    io->flags = 0;
    io->length = static_cast<__u32>(n);
    if (in) std::copy_n(data.begin(), n, io->data);
    int rc = ioctl(fd, in ? USB_RAW_IOCTL_EP0_WRITE : USB_RAW_IOCTL_EP0_READ, io);
    if (rc < 0) throw std::runtime_error(std::string(in ? "EP0_WRITE" : "EP0_READ") + " failed: " + std::strerror(errno));

    std::vector<std::uint8_t> got;
    const auto completed = std::min<std::size_t>(n, static_cast<std::size_t>(std::max(rc, 0)));
    if (!in && completed > 0) got.assign(io->data, io->data + completed);
    const auto& payload = in ? data : got;
    const auto payload_length = in ? completed : got.size();

    Json ev = Json::Object{};
    ev["event"] = in ? "CONTROL_RESPONSE_COMPLETE" : "CONTROL_OUT_COMPLETE";
    ev["state"] = "ENUMERATING";
    ev["endpoint_address"] = 0;
    ev["direction"] = in ? "device_to_host" : "host_to_device";
    ev["request"] = request;
    ev["requested_length"] = n;
    ev["actual_length"] = payload_length;
    ev["status"] = "completed";
    if (payload_length > 0) {
        std::vector<std::uint8_t> actual(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(payload_length));
        ev["payload_hex"] = hex_bytes(actual);
    }
    trace.log(std::move(ev));
    return got;
}
std::vector<std::uint8_t> match_custom_response(const Json& root,const usb_ctrlrequest& ctrl) {
    const auto& responses=root.at("control_responses"); if(!responses.is_array())return {};
    auto rt=ctrl.bRequestType,req=ctrl.bRequest;auto val=le16(ctrl.wValue),idx=le16(ctrl.wIndex);
    for(const auto& r:responses.as_array()){if(field_u(r,"bmRequestType",256)!=rt||field_u(r,"bRequest",256)!=req)continue;if(r.contains("wValue")&&field_u(r,"wValue",val)!=val)continue;if(r.contains("wIndex")&&field_u(r,"wIndex",idx)!=idx)continue;return parse_hex_bytes(r.at("data_hex").as_string());}return {};
}
struct EndpointHandle {
    std::uint32_t handle{};
    std::uint8_t address{};
    std::uint16_t max_packet{};
    bool in{};
    std::uint8_t transfer_type{};
    std::vector<std::uint8_t> in_payload;
    bool repeat_in{};
    std::atomic<bool> stop{false};
    std::thread worker;
};

std::vector<std::uint8_t> configured_in_payload(const Json& root, std::uint8_t address) {
    const auto& payloads=root.at("endpoint_io").at("in_payloads");
    if(!payloads.is_object()) return {};
    for(const auto& [k,v]:payloads.as_object()) {
        try { if(std::stoul(k,nullptr,0)==address) return parse_hex_bytes(v.as_string()); } catch(...) {}
    }
    return {};
}

void endpoint_worker_loop(int fd, EndpointHandle* ep, std::size_t out_limit, TraceLogger* trace) {
    if(ep->transfer_type==1) {
        Json e=Json::Object{};e["endpoint_address"]=ep->address;e["reason"]="isochronous data worker is not enabled in this version";trace->event("ENDPOINT_IO_UNSUPPORTED","CONFIGURED",e);return;
    }
    if(ep->in) {
        bool once=false;
        while(!ep->stop.load() && (!once || ep->repeat_in)) {
            Json queued=Json::Object{};queued["endpoint_address"]=ep->address;queued["direction"]="device_to_host";queued["payload_hex"]=hex_bytes(ep->in_payload);queued["requested_length"]=ep->in_payload.size();trace->event("DEVICE_IN_TRANSFER_QUEUED","ACTIVE",queued);
            std::vector<std::uint8_t> mem(sizeof(usb_raw_ep_io)+ep->in_payload.size());auto* io=reinterpret_cast<usb_raw_ep_io*>(mem.data());io->ep=static_cast<__u16>(ep->handle);io->flags=0;io->length=static_cast<__u32>(ep->in_payload.size());std::copy(ep->in_payload.begin(),ep->in_payload.end(),io->data);
            int rc=ioctl(fd,USB_RAW_IOCTL_EP_WRITE,io);
            if(rc<0){if(ep->stop.load()||errno==ESHUTDOWN||errno==EINTR)break;Json err=Json::Object{};err["endpoint_address"]=ep->address;err["errno"]=errno;err["message"]=std::strerror(errno);trace->event("DEVICE_IN_TRANSFER_ERROR","ACTIVE",err);break;}
            Json done=Json::Object{};done["endpoint_address"]=ep->address;done["direction"]="device_to_host";done["payload_hex"]=hex_bytes(ep->in_payload);done["requested_length"]=ep->in_payload.size();done["actual_length"]=rc;done["status"]="completed";trace->event("DEVICE_IN_TRANSFER_COMPLETE","ACTIVE",done);once=true;
        }
    } else {
        out_limit=std::clamp<std::size_t>(out_limit,ep->max_packet,std::max<std::size_t>(ep->max_packet,65536));
        while(!ep->stop.load()) {
            std::vector<std::uint8_t> mem(sizeof(usb_raw_ep_io)+out_limit);auto* io=reinterpret_cast<usb_raw_ep_io*>(mem.data());io->ep=static_cast<__u16>(ep->handle);io->flags=0;io->length=static_cast<__u32>(out_limit);
            int rc=ioctl(fd,USB_RAW_IOCTL_EP_READ,io);
            if(rc<0){if(ep->stop.load()||errno==ESHUTDOWN||errno==EINTR)break;Json err=Json::Object{};err["endpoint_address"]=ep->address;err["errno"]=errno;err["message"]=std::strerror(errno);trace->event("HOST_OUT_TRANSFER_ERROR","ACTIVE",err);break;}
            Json event=Json::Object{};event["endpoint_address"]=ep->address;event["requested_length"]=out_limit;event["actual_length"]=rc;event["direction"]="host_to_device";event["status"]="completed";if(rc>0)event["payload_hex"]=hex_bytes(std::vector<std::uint8_t>(io->data,io->data+rc));trace->event("DEVICE_OUT_TRANSFER_RECEIVED","ACTIVE",event);
        }
    }
}

void disable_endpoints(int fd, std::vector<std::unique_ptr<EndpointHandle>>& endpoints,
                       TraceLogger& trace, bool allow_inactive = false) {
    if (endpoints.empty()) return;

    // Raw Gadget EP_READ/EP_WRITE are blocking and keep an URB queued until
    // completion. Stop every worker first; an UDC reset can already have
    // disabled the endpoint before user space receives RESET/DISCONNECT.
    for (auto& ep : endpoints) ep->stop.store(true);
    for (auto& ep : endpoints) {
        if (ep->worker.joinable())
            (void)pthread_kill(ep->worker.native_handle(), SIGUSR1);
    }
    for (auto& ep : endpoints) {
        if (ep->worker.joinable()) ep->worker.join();
    }

    std::vector<std::string> failures;
    for (auto it = endpoints.rbegin(); it != endpoints.rend(); ++it) {
        const auto handle = (*it)->handle;
        if (ioctl(fd, USB_RAW_IOCTL_EP_DISABLE,
                  static_cast<unsigned long>(handle)) < 0) {
            const int saved_errno = errno;
            Json e = Json::Object{};
            e["endpoint_handle"] = handle;
            e["endpoint_address"] = (*it)->address;
            e["errno"] = saved_errno;
            e["message"] = std::strerror(saved_errno);
            e["workers_joined_first"] = true;
            const bool already_inactive = allow_inactive &&
                (saved_errno == EBUSY || saved_errno == ESHUTDOWN || saved_errno == ENODEV);
            if (already_inactive) {
                e["note"] = "UDC may have already disabled this endpoint during reset/disconnect; treating as inactive";
                trace.event("ENDPOINT_ALREADY_INACTIVE", "RESETTING", e);
            } else {
                trace.event("ENDPOINT_DISABLE_ERROR", "CONFIGURED", e);
                failures.push_back("handle=" + std::to_string(handle) +
                                   " address=" + std::to_string((*it)->address) +
                                   ": " + std::strerror(saved_errno));
            }
        } else {
            Json e = Json::Object{};
            e["endpoint_handle"] = handle;
            e["endpoint_address"] = (*it)->address;
            trace.event("ENDPOINT_DISABLED", "ADDRESSED", e);
        }
    }

    // All workers are joined now. Never reuse their handles after a reset,
    // even if the UDC already disabled them and EP_DISABLE returned EBUSY.
    endpoints.clear();

    if (!failures.empty()) {
        std::ostringstream message;
        message << "Raw Gadget could not disable endpoint(s) after stopping all workers: ";
        for (std::size_t i = 0; i < failures.size(); ++i) {
            if (i) message << "; ";
            message << failures[i];
        }
        throw std::runtime_error(message.str());
    }
}

void enable_config_endpoints(int fd,const CompiledProfile& p,std::uint8_t configuration,std::vector<std::unique_ptr<EndpointHandle>>& handles,TraceLogger& trace) {
    const Json& cs=p.source.at("configurations");
    const Json* selected=nullptr;for(const auto& c:cs.as_array())if(field_u(c,"bConfigurationValue",1)==configuration){selected=&c;break;}
    if(!selected)throw std::runtime_error("host selected unknown configuration value "+std::to_string(configuration));
    const bool repeat_in=p.source.at("endpoint_io").at("repeat_in").as_bool(false);
    for(const auto& iface:selected->at("interfaces").as_array()) {
        if(field_u(iface,"bAlternateSetting",0)!=0)continue;
        for(const auto& ep:iface.at("endpoints").as_array()) {
            auto desc=usb_endpoint_descriptor{};desc.bLength=USB_DT_ENDPOINT_SIZE;desc.bDescriptorType=USB_DT_ENDPOINT;desc.bEndpointAddress=static_cast<__u8>(field_u(ep,"bEndpointAddress",0x81));desc.bmAttributes=static_cast<__u8>(field_u(ep,"bmAttributes",2));auto packet=field_u(ep,"wMaxPacketSize",64);desc.wMaxPacketSize=static_cast<__le16>((packet&0xff) | (packet&0xff00));desc.bInterval=static_cast<__u8>(field_u(ep,"bInterval",0));
            int h=ioctl(fd,USB_RAW_IOCTL_EP_ENABLE,&desc);if(h<0){Json e=Json::Object{};e["endpoint_address"]=desc.bEndpointAddress;e["errno"]=errno;e["message"]=std::strerror(errno);trace.event("ENDPOINT_ENABLE_ERROR","ENUMERATING",e);throw std::runtime_error("UDC cannot enable endpoint address "+std::to_string(desc.bEndpointAddress)+": "+std::strerror(errno));}
            auto worker=std::make_unique<EndpointHandle>();worker->handle=static_cast<std::uint32_t>(h);worker->address=desc.bEndpointAddress;worker->max_packet=static_cast<std::uint16_t>(packet);worker->in=(desc.bEndpointAddress&USB_DIR_IN)!=0;worker->transfer_type=desc.bmAttributes&USB_ENDPOINT_XFERTYPE_MASK;worker->repeat_in=repeat_in;worker->in_payload=configured_in_payload(p.source,worker->address);
            Json e=Json::Object{};e["endpoint_address"]=desc.bEndpointAddress;e["endpoint_handle"]=h;e["max_packet_size"]=packet;trace.event("ENDPOINT_ENABLED","CONFIGURED",e);
            handles.push_back(std::move(worker));
        }
    }
}

void start_endpoint_workers(int fd,
                            std::vector<std::unique_ptr<EndpointHandle>>& endpoints,
                            std::size_t out_limit, TraceLogger& trace) {
    for (auto& worker : endpoints) {
        if (worker->worker.joinable()) continue;
        if (!worker->in || !worker->in_payload.empty()) {
            auto* ptr = worker.get();
            ptr->worker = std::thread([fd, ptr, out_limit, &trace]() {
                endpoint_worker_loop(fd, ptr, out_limit, &trace);
            });
        } else {
            Json no = Json::Object{};
            no["endpoint_address"] = worker->address;
            no["reason"] = "no endpoint_io.in_payloads entry for this IN endpoint; it is enabled but no IN request is queued";
            trace.event("ENDPOINT_IN_NO_PAYLOAD", "CONFIGURED", no);
        }
    }
}

void handle_control(int fd,const CompiledProfile& profile,const usb_ctrlrequest& ctrl,std::uint8_t& active_config, std::vector<std::unique_ptr<EndpointHandle>>& endpoints,TraceLogger& trace) {
    const std::uint8_t rt=ctrl.bRequestType, req=ctrl.bRequest; const std::uint16_t val=le16(ctrl.wValue),idx=le16(ctrl.wIndex),len=le16(ctrl.wLength); const bool in=(rt&USB_DIR_IN)!=0;
    Json reqj=Json::Object{};reqj["bmRequestType"]=rt;reqj["bRequest"]=req;reqj["wValue"]=val;reqj["wIndex"]=idx;reqj["wLength"]=len;reqj["direction"]=in?"device_to_host":"host_to_device";
    Json ej=Json::Object{};ej["request"]=reqj;ej["requested_length"]=len;trace.event("CONTROL_REQUEST","ENUMERATING",ej);
    std::vector<std::uint8_t> response;
    if(in) {
        if((rt&USB_TYPE_MASK)==USB_TYPE_STANDARD && req==USB_REQ_GET_DESCRIPTOR) response=get_descriptor(profile,static_cast<std::uint8_t>(val>>8),static_cast<std::uint8_t>(val&0xff),idx);
        else if((rt&USB_TYPE_MASK)==USB_TYPE_STANDARD && req==USB_REQ_GET_CONFIGURATION) response={active_config};
        else if((rt&USB_TYPE_MASK)==USB_TYPE_STANDARD && req==USB_REQ_GET_STATUS) response={0,0};
        else if((rt&USB_TYPE_MASK)==USB_TYPE_STANDARD && req==USB_REQ_GET_INTERFACE) response={0};
        else response=match_custom_response(profile.source,ctrl);
        if(response.empty() && len>0){ioctl(fd,USB_RAW_IOCTL_EP0_STALL);Json e=Json::Object{};e["request"]=reqj;e["reason"]="no response configured";trace.event("CONTROL_STALL","ENUMERATING",e);return;}
        try{transact_ep0(fd, true, response, len, trace, reqj);}catch(const std::exception& e){Json x=Json::Object{};x["error"]=e.what();trace.event("CONTROL_RESPONSE_ERROR","ENUMERATING",x);throw;}
    } else {
        const bool set_config=(rt&USB_TYPE_MASK)==USB_TYPE_STANDARD && req==USB_REQ_SET_CONFIGURATION;
        const std::uint8_t requested_config=static_cast<std::uint8_t>(val&0xff);
        if(set_config) {
            // SET_CONFIGURATION can follow a DWC2 reset event. In that case the
            // UDC may have disabled old endpoints before user space fetched the
            // event; stale handles are safe to discard after all workers stop.
            disable_endpoints(fd, endpoints, trace, true);
            if(requested_config!=0) {
                enable_config_endpoints(fd,profile,requested_config,endpoints,trace);
                check_ioctl(fd,USB_RAW_IOCTL_CONFIGURE,nullptr,"USB_RAW_IOCTL_CONFIGURE");
            }
        }
        std::vector<std::uint8_t> payload;
        try { payload=transact_ep0(fd, false, {}, len, trace, reqj); } catch(const std::exception& e){Json x=Json::Object{};x["error"]=e.what();trace.event("CONTROL_OUT_ERROR","ENUMERATING",x);throw;}
        if(set_config) {
            active_config=requested_config;
            Json e=Json::Object{};e["configuration_value"]=requested_config;
            trace.event("SET_CONFIGURATION",requested_config?"CONFIGURED":"ADDRESSED",e);
            // Do not queue interrupt/Bulk worker requests until the host's
            // SET_CONFIGURATION status stage has completed successfully.
            if (requested_config != 0) {
                const auto out_limit = field_u(profile.source.at("endpoint_io"), "out_buffer_bytes", 16384);
                start_endpoint_workers(fd, endpoints, out_limit, trace);
            }
        } else if((rt&USB_TYPE_MASK)==USB_TYPE_STANDARD && req==USB_REQ_SET_INTERFACE) {
            Json e=Json::Object{};e["interface_number"]=idx;e["alternate_setting"]=val;trace.event("SET_INTERFACE_OBSERVED","CONFIGURED",e);
        } else if((rt&USB_TYPE_MASK)==USB_TYPE_STANDARD && req==USB_REQ_SET_ADDRESS) {
            Json e=Json::Object{};e["address"]=val;trace.event("SET_ADDRESS_OBSERVED","ENUMERATING",e);
        }
    }
}
}
int probe_usb_device_mode(){
    std::cout<<"USB Device/Gadget capability probe (Linux)\n";
    const bool raw=std::filesystem::exists("/dev/raw-gadget");std::cout<<"/dev/raw-gadget: "<<(raw?"present":"not found")<<"\n";
    const std::filesystem::path udcdir("/sys/class/udc");bool found=false;
    if(std::filesystem::exists(udcdir)){for(const auto& e:std::filesystem::directory_iterator(udcdir)){found=true;auto name=e.path().filename().string();std::string driver="(unknown)";{std::ifstream uevent(e.path()/"uevent");std::string line;while(std::getline(uevent,line)){const std::string prefix="USB_UDC_NAME=";if(line.rfind(prefix,0)==0){driver=line.substr(prefix.size());break;}}}std::cout<<"UDC device name: "<<name<<"\n  Raw Gadget driver name from uevent: "<<driver<<"\n  candidate command: sudo ./usb-connect run profiles/valid/minimal-device.json --backend raw-gadget --udc-driver '"<<driver<<"' --udc-device '"<<name<<"' --trace runs/test-001/device.jsonl\n";}}
    if(!found)std::cout<<"No controller found under /sys/class/udc. Confirm USB-C device mode, DWC2 configuration, and kernel UDC driver.\n";
    if(!raw)std::cout<<"Raw Gadget missing. Kernel must support CONFIG_USB_RAW_GADGET and expose /dev/raw-gadget.\n";
    return (raw&&found)?0:2;
}
int run_raw_gadget(const RawGadgetOptions& o,const CompiledProfile& profile){
    if(!std::filesystem::exists("/dev/raw-gadget"))throw std::runtime_error("/dev/raw-gadget is absent; kernel/module must enable CONFIG_USB_RAW_GADGET");
    if(o.udc_driver.empty()||o.udc_device.empty())throw std::runtime_error("pass both --udc-driver and --udc-device; run usb-connect probe to inspect UDC");
    int speed=USB_SPEED_HIGH;if(o.speed=="full")speed=USB_SPEED_FULL;else if(o.speed=="low")speed=USB_SPEED_LOW;else if(o.speed!="high")throw std::runtime_error("speed must be high, full or low");
    TraceLogger trace(o.trace_path,o.run_id,"raspberry_pi"); Json start=Json::Object{};start["profile_name"]=profile.name;start["backend"]="raw-gadget";start["udc_driver"]=o.udc_driver;start["udc_device"]=o.udc_device;start["speed"]=o.speed;start["research_mode"]=profile.research_mode;trace.event("BACKEND_STARTING","BACKEND_READY",start);
    int fd=open("/dev/raw-gadget",O_RDWR);if(fd<0)throw std::runtime_error(std::string("open /dev/raw-gadget failed: ")+std::strerror(errno));
    std::vector<std::unique_ptr<EndpointHandle>> endpoints;
    try {
        usb_raw_init init{};std::strncpy(reinterpret_cast<char*>(init.driver_name),o.udc_driver.c_str(),sizeof(init.driver_name)-1);std::strncpy(reinterpret_cast<char*>(init.device_name),o.udc_device.c_str(),sizeof(init.device_name)-1);init.speed=static_cast<__u8>(speed);
        check_ioctl(fd,USB_RAW_IOCTL_INIT,&init,"USB_RAW_IOCTL_INIT");check_ioctl(fd,USB_RAW_IOCTL_RUN,nullptr,"USB_RAW_IOCTL_RUN");
        struct sigaction sa{};sa.sa_handler=on_signal;sigemptyset(&sa.sa_mask);sa.sa_flags=0;sigaction(SIGINT,&sa,nullptr);sigaction(SIGTERM,&sa,nullptr);
        struct sigaction worker_interrupt{};worker_interrupt.sa_handler=on_worker_interrupt;sigemptyset(&worker_interrupt.sa_mask);worker_interrupt.sa_flags=0;sigaction(SIGUSR1,&worker_interrupt,nullptr);
        trace.event("BACKEND_RUNNING","BACKEND_READY");std::uint8_t active_config=0;std::cout<<"Raw Gadget running. Press Ctrl+C to stop. Trace: "<<o.trace_path<<"\n";
        while(!stop_requested){alignas(8) std::array<std::uint8_t,256> buf{};auto* ev=reinterpret_cast<usb_raw_event*>(buf.data());ev->length=buf.size()-sizeof(usb_raw_event);int rc=ioctl(fd,USB_RAW_IOCTL_EVENT_FETCH,ev);if(rc<0){if(errno==EINTR&&stop_requested)break;if(errno==EINTR)continue;throw std::runtime_error(std::string("USB_RAW_IOCTL_EVENT_FETCH failed: ")+std::strerror(errno));}
            switch(ev->type){case USB_RAW_EVENT_CONNECT:{trace.event("UDC_BOUND","BACKEND_READY");usb_raw_eps_info eps{};int n=ioctl(fd,USB_RAW_IOCTL_EPS_INFO,&eps);Json e=Json::Object{};e["available_endpoint_count"]=n;if(n>0){Json arr=Json::Array{};for(int i=0;i<std::min(n,USB_RAW_EPS_NUM_MAX);++i){Json x=Json::Object{};x["name"]=reinterpret_cast<const char*>(eps.eps[i].name);x["address"]=eps.eps[i].addr;x["maxpacket_limit"]=eps.eps[i].limits.maxpacket_limit;x["bulk"]=static_cast<bool>(eps.eps[i].caps.type_bulk);x["interrupt"]=static_cast<bool>(eps.eps[i].caps.type_int);x["isochronous"]=static_cast<bool>(eps.eps[i].caps.type_iso);arr.as_array().push_back(std::move(x));}e["endpoints"]=std::move(arr);}trace.event("ENDPOINT_CAPABILITIES","ATTACHED",e);break;}
                case USB_RAW_EVENT_CONTROL:{if(ev->length<sizeof(usb_ctrlrequest)){trace.event("MALFORMED_CONTROL_EVENT","ERROR");break;}usb_ctrlrequest ctrl{};std::memcpy(&ctrl,ev->data,sizeof(ctrl));handle_control(fd,profile,ctrl,active_config,endpoints,trace);break;}
                case USB_RAW_EVENT_SUSPEND:trace.event("USB_SUSPEND","SUSPENDED");break;
                case USB_RAW_EVENT_RESUME:trace.event("USB_RESUME",active_config?"CONFIGURED":"ENUMERATING");break;
                case USB_RAW_EVENT_RESET:{
                    disable_endpoints(fd, endpoints, trace, true);
                    active_config=0;
                    trace.event("USB_RESET","ENUMERATING");
                    break;
                }
                case USB_RAW_EVENT_DISCONNECT:{
                    // The Raw Gadget ABI explicitly documents that DWC2 may
                    // report bus reset as DISCONNECT. Do not declare a physical
                    // cable pull and do not terminate Raw Gadget on this event.
                    Json e=Json::Object{};
                    e["classification"]="reset_or_disconnect";
                    e["note"]="UDC reported USB_RAW_EVENT_DISCONNECT; on DWC2 this may mean bus reset rather than physical cable removal";
                    disable_endpoints(fd, endpoints, trace, true);
                    active_config=0;
                    trace.event("USB_RESET_OR_DISCONNECT","ENUMERATING",e);
                    break;
                }
                default:{Json e=Json::Object{};e["raw_event_type"]=ev->type;trace.event("UNKNOWN_RAW_GADGET_EVENT","UNKNOWN",e);break;}}
        }
        disable_endpoints(fd, endpoints, trace, true);trace.event("BACKEND_STOPPED","DETACHED");close(fd);return 0;
    } catch(...) {
        auto original_error = std::current_exception();
        try {
            disable_endpoints(fd, endpoints, trace, true);
        } catch (const std::exception& cleanup_error) {
            Json e = Json::Object{};
            e["message"] = cleanup_error.what();
            trace.event("BACKEND_CLEANUP_ERROR", "ERROR", e);
        }
        close(fd);
        std::rethrow_exception(original_error);
    }
}
#else
int probe_usb_device_mode(){std::cerr<<"Raw Gadget backend is only available on Linux.\n";return 2;}
int run_raw_gadget(const RawGadgetOptions&,const CompiledProfile&){throw std::runtime_error("Raw Gadget backend is only available on Linux");}
#endif
}
