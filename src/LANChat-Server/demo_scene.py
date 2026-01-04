#!/usr/bin/env python3
# test_client.py - 简单的命令行聊天客户端
import socket
import threading
import time
import sys

class SimpleChatClient:
    def __init__(self, host='127.0.0.1', port=8888, username='TestUser'):
        self.host = host
        self.port = port
        self.username = username
        self.socket = None
        self.running = False
        
    def connect(self):
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((self.host, self.port))
            print(f"[{self.username}] 连接到服务器 {self.host}:{self.port}")
            
            # 发送登录信息
            login_msg = f"LOGIN:{self.username}\n"
            self.socket.send(login_msg.encode())
            
            self.running = True
            
            # 启动接收线程
            recv_thread = threading.Thread(target=self.receive_messages)
            recv_thread.daemon = True
            recv_thread.start()
            
            return True
            
        except Exception as e:
            print(f"[{self.username}] 连接失败: {e}")
            return False
    
    def receive_messages(self):
        while self.running:
            try:
                data = self.socket.recv(1024)
                if not data:
                    break
                    
                message = data.decode().strip()
                if message:
                    print(f"\n[{self.username}] 收到: {message}")
                    print(f"[{self.username}] 输入消息: ", end='', flush=True)
                    
            except:
                break
    
    def send_message(self, message):
      if not self.socket:
          return False
          
      try:
          # 修改格式为 Qt 客户端期望的格式
          import time
          current_time = time.strftime("%H:%M")
          formatted_msg = f"[{current_time}] {self.username}: {message}\n"
          self.socket.send(formatted_msg.encode())
          print(f"[{self.username}] 已发送: {message}")
          return True
      except Exception as e:
          print(f"[{self.username}] 发送失败: {e}")
          return False
    
    def disconnect(self):
        self.running = False
        if self.socket:
            self.socket.close()
        print(f"[{self.username}] 已断开连接")

def run_client(username, host='127.0.0.1', port=8888):
    """运行一个测试客户端"""
    client = SimpleChatClient(host, port, username)
    
    if not client.connect():
        return
    
    print(f"\n[{username}] 客户端已启动")
    print(f"[{username}] 输入消息 (输入 '/quit' 退出):")
    
    try:
        while client.running:
            message = input(f"[{username}] 输入消息: ").strip()
            
            if message.lower() == '/quit':
                break
            elif message:
                client.send_message(message)
                
    except KeyboardInterrupt:
        print(f"\n[{username}] 用户中断")
    finally:
        client.disconnect()

if __name__ == "__main__":
    # 用法: python test_client.py [用户名] [服务器IP] [端口]
    username = sys.argv[1] if len(sys.argv) > 1 else "测试用户"
    host = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"
    port = int(sys.argv[3]) if len(sys.argv) > 3 else 8888
    
    run_client(username, host, port)