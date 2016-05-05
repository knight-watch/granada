/**
  * Copyright (c) <2016> granada <afernandez@cookinapps.io>
  *
  * This source code is licensed under the MIT license.
  *
  * Permission is hereby granted, free of charge, to any person obtaining a copy
  * of this software and associated documentation files (the "Software"), to deal
  * in the Software without restriction, including without limitation the rights
  * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  * copies of the Software, and to permit persons to whom the Software is
  * furnished to do so, subject to the following conditions:
  *
  * The above copyright notice and this permission notice shall be included in
  * all copies or substantial portions of the Software.
  *
  * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  * SOFTWARE.
  *
  * Manage the lifecycle of the sessions, store them using Redis.
  *
  */

#include "granada/http/session/redis_session_handler.h"

namespace granada{
  namespace http{
    namespace session{

      RedisSessionHandler::RedisSessionHandler(std::shared_ptr<granada::http::session::Session> reference_session) : SessionHandler(){

        reference_session_ = reference_session;

        LoadProperties();

        n_generator_ = std::unique_ptr<utility::nonce_generator>(new utility::nonce_generator(token_length_));
        sessions_ = std::unique_ptr<granada::cache::RedisCacheDriver>(new granada::cache::RedisCacheDriver());

        //thread for cleaning the sessions.
        if (clean_sessions_frequency_>-1){
          pplx::create_task([this]{
            this->CleanSessions(true);
          });
        }
      }

      void RedisSessionHandler::LoadProperties(){
        std::string clean_sessions_frequency_str(GetProperty("session_clean_frequency"));
        if (clean_sessions_frequency_str.empty()){
          clean_sessions_frequency_ = DEFAULT_CLEAN_SESSIONS_FREQUENCY;
        }else{
          try{
            clean_sessions_frequency_ = std::stod(clean_sessions_frequency_str);
          }catch(const std::exception& e){
            clean_sessions_frequency_ = DEFAULT_CLEAN_SESSIONS_FREQUENCY;
          }
        }
        std::string token_length_str(GetProperty("session_token_length"));
        if (token_length_str.empty()){
          token_length_ = DEFAULT_TOKEN_LENGTH;
        }else{
          try{
            token_length_ = std::stoi(token_length_str);
          }catch(const std::exception& e){
            token_length_ = DEFAULT_TOKEN_LENGTH;
          }
        }
      }

      const std::string RedisSessionHandler::GetProperty(const std::string& name){
        return granada::util::application::GetProperty(name);
      }

      const bool RedisSessionHandler::SessionExists(const std::string& token){
        if (!token.empty()){
          return sessions_->Exists("session:value:"+token);
        }
        return false;
      }

      const std::string RedisSessionHandler::GenerateToken(){
        return RedisSessionHandler::n_generator_->generate();
      }

      void RedisSessionHandler::LoadSession(const std::string& token,granada::http::session::Session* virgin){
        if (!token.empty()){
          std::string update_time_str = sessions_->Read("session:value:" + token, "update.time");
          time_t update_time = 0;
          try{
            long int seconds = std::stol(update_time_str);
            update_time = seconds;
          }catch(const std::logic_error& e){}
          virgin->set(token,update_time);
          if (!virgin->IsValid()){
            virgin->set("",0);
          }
        }
      }

      void RedisSessionHandler::SaveSession(std::shared_ptr<granada::http::session::Session> session){
        std::string token = session->GetToken();
        if (!token.empty()){
          std::stringstream ss;
          ss << session->GetUpdateTime();
          std::string update_time_str = ss.str();
          sessions_->Write("session:value:" + token, "token", token);
          sessions_->Write("session:value:" + token, "update.time", update_time_str);
        }
      }

      void RedisSessionHandler::DeleteSession(granada::http::session::Session* session){
        std::string token = session->GetToken();
        if (!token.empty()){
          granada::cache::RedisIterator redis_iterator(granada::cache::RedisIterator::Type::KEYS,"session:*" + token + "*");
          while(redis_iterator.has_next()){
            sessions_->Destroy(redis_iterator.next());
          }
        }
      }

      void RedisSessionHandler::CleanSessions(){

        granada::cache::RedisIterator redis_iterator(granada::cache::RedisIterator::Type::SCAN,"session:value:*");
        while(redis_iterator.has_next()){
          std::string key = redis_iterator.next();
          std::string token = sessions_->Read(key, "token");
          std::string update_time_str = sessions_->Read(key, "update.time");
          time_t update_time = 0;
          try{
            long int seconds = std::stol(update_time_str);
            update_time = seconds;
          }catch(const std::logic_error& e){}
          reference_session_->set(token,update_time);
          if (reference_session_->IsGarbage()){
            reference_session_->Close();
          }
        }
      }

      void RedisSessionHandler::CleanSessions(bool recursive){
        CleanSessions();

        if (clean_sessions_frequency_>-1){
          // wait for x seconds...
          std::this_thread::sleep_for (std::chrono::duration<double>(clean_sessions_frequency_));
          // ... and clean sessions again.
          CleanSessions(true);
        }
      }

    }
  }
}
