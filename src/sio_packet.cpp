//
//  sio_packet.cpp
//
//  Created by Melo Yao on 3/22/15.
//

#include "sio_packet.h"
#include <rapidjson/document.h>
#include <rapidjson/encodedstream.h>
#include <rapidjson/writer.h>
#include <cassert>
namespace sio
{
    using namespace rapidjson;
    using namespace std;
    void accept_message(message const& msg,Value& val, Document& doc,vector<shared_ptr<const string> >& buffers);
    
    void accept_int_message(int_message const& msg, Value& val)
    {
        val.SetInt64(msg.get_int());
    }
    
    void accept_double_message(double_message const& msg, Value& val)
    {
        val.SetDouble(msg.get_double());
    }

    void accept_string_message(string_message const& msg, Value& val)
    {
        val.SetString(msg.get_string().data(),(SizeType) msg.get_string().length());
    }

    
    void accept_binary_message(binary_message const& msg,Value& val,Document& doc,vector<shared_ptr<const string> >& buffers)
    {
        val.SetObject();
        Value boolVal;
        boolVal.SetBool(true);
        val.AddMember("_placeholder", boolVal, doc.GetAllocator());
        Value numVal;
        numVal.SetInt((int)buffers.size());
        val.AddMember("num", numVal, doc.GetAllocator());
        //FIXME can not avoid binary copy here.
        shared_ptr<string> write_buffer = make_shared<string>();
        write_buffer->reserve(msg.get_binary()->size()+1);
        char frame_char = packet::frame_message;
        write_buffer->append(&frame_char,1);
        write_buffer->append(*(msg.get_binary()));
        buffers.push_back(write_buffer);
    }
    
    void accept_array_message(array_message const& msg,Value& val,Document& doc,vector<shared_ptr<const string> >& buffers)
    {
        val.SetArray();
        for (vector<message::ptr>::const_iterator it = msg.get_vector().begin(); it!=msg.get_vector().end(); ++it) {
            Value child;
            accept_message(*(*it), child, doc,buffers);
            val.PushBack(child, doc.GetAllocator());
        }
    }
    
    void accept_object_message(object_message const& msg,Value& val,Document& doc,vector<shared_ptr<const string> >& buffers)
    {
        val.SetObject();
        for (map<string,message::ptr>::const_iterator it = msg.get_map().begin(); it!= msg.get_map().end(); ++it) {
            Value nameVal;
            nameVal.SetString(it->first.data(), (SizeType)it->first.length(), doc.GetAllocator());
            Value valueVal;
            accept_message(*(it->second), valueVal, doc,buffers);
            val.AddMember(nameVal, valueVal, doc.GetAllocator());
        }
    }
    
    void accept_message(message const& msg,Value& val, Document& doc,vector<shared_ptr<const string> >& buffers)
    {
        const message* msg_ptr = &msg;
        switch(msg.get_flag())
        {
            case message::flag_integer:
            {
                accept_int_message(*(static_cast<const int_message*>(msg_ptr)), val);
                break;
            }
            case message::flag_double:
            {
                accept_double_message(*(static_cast<const double_message*>(msg_ptr)), val);
                break;
            }
            case message::flag_string:
            {
                accept_string_message(*(static_cast<const string_message*>(msg_ptr)), val);
                break;
            }
            case message::flag_binary:
            {
                accept_binary_message(*(static_cast<const binary_message*>(msg_ptr)), val,doc,buffers);
                break;
            }
            case message::flag_array:
            {
                accept_array_message(*(static_cast<const array_message*>(msg_ptr)), val,doc,buffers);
                break;
            }
            case message::flag_object:
            {
                accept_object_message(*(static_cast<const object_message*>(msg_ptr)), val,doc,buffers);
                break;
            }
            default:
                break;
        }
    }
    
    message::ptr from_json(Value const& value, vector<shared_ptr<const string> > const& buffers)
    {
        if(value.IsInt64())
        {
            return int_message::create(value.GetInt64());
        }
        else if(value.IsDouble())
        {
            return double_message::create(value.GetDouble());
        }
        else if(value.IsString())
        {
            string str(value.GetString(),value.GetStringLength());
            return string_message::create(str);
        }
        else if(value.IsArray())
        {
            message::ptr ptr = array_message::create();
            for (SizeType i = 0; i< value.Size(); ++i) {
                static_cast<array_message*>(ptr.get())->get_vector().push_back(from_json(value[i],buffers));
            }
            return ptr;
        }
        else if(value.IsObject())
        {
             //binary placeholder
            if (value.HasMember("_placeholder") && value["_placeholder"].GetBool()) {
               
                int num = value["num"].GetInt();
                if(num > 0 && num < buffers.size())
                {
                    return binary_message::create(buffers[num]);
                }
                return message::ptr();
            }
            //real object message.
            message::ptr ptr = object_message::create();
            for (auto it = value.MemberBegin();it!=value.MemberEnd();++it)
            {
                if(it->name.IsString())
                {
                    string key(it->name.GetString(),it->name.GetStringLength());
                    static_cast<object_message*>(ptr.get())->get_map()[key] = from_json(it->value,buffers);
                }
            }
            return ptr;
        }
        return message::ptr();
    }
    
    packet::packet(string const& nsp,message::ptr const& msg,int pack_id, bool isAck):
    _frame(frame_message),
    _type((isAck?type_ack : type_event) | type_undetermined),
    _nsp(nsp),
    _message(msg),
    _pack_id(pack_id),
    _pending_buffers(0)
    {
        assert((!isAck
                || (isAck&&pack_id>=0)));
    }

    packet::packet(type type,message::ptr const& msg):
    _frame(frame_message),
    _type(type),
    _message(msg),
    _pack_id(-1),
    _pending_buffers(0)
    {
        
    }
    
    packet::packet(packet::frame_type frame):
    _frame(frame),
    _type(type_undetermined),
    _pack_id(-1),
    _pending_buffers(0)
    {
        
    }
    
    packet::packet():
    _type(type_undetermined),
    _pack_id(-1),
    _pending_buffers(0)
    {
        
    }
    
    
    bool packet::is_binary_message(string const& payload_ptr)
    {
        return payload_ptr.size()>0 && payload_ptr[0] == frame_message;
    }
    
    bool packet::is_text_message(string const& payload_ptr)
    {
        return payload_ptr.size()>0 && payload_ptr[0] == (frame_message + '0');
    }
    
    bool packet::is_message(string const& payload_ptr)
    {
        return is_binary_message(payload_ptr) || is_text_message(payload_ptr);
    }
    
    bool packet::parse_buffer(const string &buf_payload)
    {
        if (_pending_buffers > 0) {
            assert(is_binary_message(buf_payload));//this is ensured by outside.
            _buffers.push_back(std::make_shared<string>(buf_payload.data()+1,buf_payload.size()-1));
            _pending_buffers--;
            if (_pending_buffers == 0) {
                
                Document doc;
                doc.Parse<0>(_buffers.front()->data());
                _buffers.erase(_buffers.begin());
                _message = from_json(doc, _buffers);
                _buffers.clear();
                return false;
            }
            return true;
        }
        return false;
    }
    
    bool packet::parse(const string& payload_ptr)
    {
        assert(!is_binary_message(payload_ptr)); //this is ensured by outside
        _frame = (packet::frame_type) (payload_ptr[0] - '0');
        
        size_t pos = 1;
        if (_frame == frame_message) {
            _type = (packet::type)(payload_ptr[pos] - '0');
            if(_type < type_min || _type > type_max)
            {
                return false;
            }
            pos++;
            if (_type == type_binary_event || _type == type_binary_ack) {
                size_t score_pos = payload_ptr.find('-');
                _pending_buffers = stoi(payload_ptr.substr(pos,score_pos));
                pos = score_pos+1;
            }
        }
        
        size_t comma_pos = payload_ptr.find_first_of("{[,");
        if( comma_pos!= string::npos && payload_ptr[comma_pos] == ',')
        {
            _nsp = payload_ptr.substr(pos,comma_pos  - pos);
            pos = comma_pos+1;
        }
        if (pos >= payload_ptr.length()) {
            //message only have type, maybe with namespace.
            return false;
        }
        size_t data_pos = payload_ptr.find_first_of("[{", pos, 2);
        if (data_pos == string::npos) {
            //we have pack id, no message.
            _pack_id = stoi(payload_ptr.substr(pos));
            return false;
        }
        else if(data_pos>pos)
        {
            //we have pack id and messages.
            _pack_id = stoi(payload_ptr.substr(pos,data_pos - pos));
        }
        if (_frame == frame_message && (_type == type_binary_event || _type == type_binary_ack)) {
            //parse later when all buffers are arrived.
            _buffers.push_back(make_shared<const string>(payload_ptr.data() + data_pos, payload_ptr.length() - data_pos));
            return true;
        }
        else
        {
            Document doc;
            doc.Parse<0>(payload_ptr.substr(data_pos).data());
            _message = from_json(doc, vector<shared_ptr<const string> >());
            return false;
        }
        
    }
    
    bool packet::accept(string& payload_ptr, vector<shared_ptr<const string> >&buffers)
    {
        char frame_char = _frame+'0';
        payload_ptr.append(&frame_char,1);
        if (_frame!=frame_message) {
            return false;
        }
        bool hasMessage = false;
        Document doc;
        if (_message) {
            accept_message(*_message, doc, doc, buffers);
            hasMessage = true;
        }
        bool hasBinary = _buffers.size()>0;
        _type = _type&(~type_undetermined);
        if(_type == type_event)
        {
            _type = hasBinary?type_binary_event:type_event;
        }
        else if(_type == type_ack)
        {
            _type = hasBinary? type_binary_ack : type_ack;
        }
        ostringstream ss;
        ss.precision(8);
        ss<<_type;
        if (hasBinary) {
            ss<<_buffers.size()<<"-";
        }
        if(_nsp.size()>0 && _nsp!="/")
        {
            ss<<_nsp;
            if (hasMessage || _pack_id>=0) {
                ss<<",";
            }
        }
        
        if(_pack_id>=0)
        {
            ss<<_pack_id;
        }
        
        payload_ptr.append(ss.str());
		if (hasMessage) {
			StringBuffer buffer;
			EncodedOutputStream<UTF8<>,StringBuffer> eos(buffer);
			Writer<EncodedOutputStream<UTF8<>,StringBuffer> > writer(eos);
            doc.Accept(writer);
			payload_ptr.append(buffer.GetString(),buffer.GetSize());
        }
		
        return hasBinary;
    }
    
    packet::frame_type packet::get_frame() const
    {
        return _frame;
    }
    
    packet::type packet::get_type() const
    {
        assert((_type & type_undetermined) == 0);
        return (type)_type;
    }
    
    string const& packet::get_nsp() const
    {
        return _nsp;
    }
    
    message::ptr const& packet::get_message() const
    {
        return _message;
    }
    
    unsigned packet::get_pack_id() const
    {
        return _pack_id;
    }
    
    
    void packet_manager::set_decode_callback(function<void (packet const&)> const& decode_callback)
    {
        m_decode_callback = decode_callback;
    }
    
    void packet_manager::set_encode_callback(function<void (bool,shared_ptr<const string> const&)> const& encode_callback)
    {
        m_encode_callback = encode_callback;
    }

    void packet_manager::reset()
    {
        m_partial_packet.reset();
    }
    
    void packet_manager::encode(packet& pack,encode_callback_function const& override_encode_callback) const
    {
        shared_ptr<string> ptr = make_shared<string>();
        vector<shared_ptr<const string> > buffers;
        const encode_callback_function *cb_ptr = &m_encode_callback;
        if(override_encode_callback)
        {
            cb_ptr = &override_encode_callback;
        }
        if(pack.accept(*ptr,buffers))
        {
            if((*cb_ptr))
            {
                (*cb_ptr)(false,ptr);
            }
            for(auto it = buffers.begin();it!=buffers.end();++it)
            {
                if((*cb_ptr))
                {
                    (*cb_ptr)(true,*it);
                }
            }
        }
        else
        {
            if((*cb_ptr))
            {
                (*cb_ptr)(false,ptr);
            }
        }
    }
    
    void packet_manager::put_payload(string const& payload)
    {
        unique_ptr<packet> p;
        do
        {
            if(packet::is_text_message(payload))
            {
                p.reset(new packet());
                if(p->parse(payload))
                {
                    m_partial_packet = std::move(p);
                }
                else
                {
                    break;
                }
            }
            else if(packet::is_binary_message(payload))
            {
                if(m_partial_packet)
                {
                    if(!m_partial_packet->parse_buffer(payload))
                    {
                        p = std::move(m_partial_packet);
                        break;
                    }
                }
            }
            else
            {
                p.reset(new packet());
                p->parse(payload);
                break;
            }
            return;
        }while(0);
        
        if(m_decode_callback)
        {
            m_decode_callback(*p);
        }
    }
}