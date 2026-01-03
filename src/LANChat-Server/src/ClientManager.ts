// src/ClientManager.ts
import { EventEmitter } from 'events';
import { createHash } from 'crypto';

export interface ClientInfo {
  id: string;           // 客户端唯一ID
  socket: any;          // 网络连接对象
  username: string;     // 用户名
  ip: string;           // IP地址
  port: number;         // 端口
  connectedAt: Date;    // 连接时间
  lastActivity: Date;   // 最后活动时间
  room?: string;        // 所在聊天室
  status: 'online' | 'away' | 'busy';  // 在线状态
}

export interface Message {
  id: string;
  type: 'chat' | 'system' | 'private' | 'file' | 'command';
  from: string;        // 发送者ID
  to?: string;         // 接收者ID（私聊用）
  room?: string;       // 房间名（群聊用）
  content: string;     // 消息内容
  timestamp: Date;     // 时间戳
  status: 'sent' | 'delivered' | 'read';  // 消息状态
}

export interface RoomInfo {
  name: string;
  clients: Set<string>;  // 客户端ID集合
  createdBy: string;
  createdAt: Date;
  isPrivate: boolean;
}

export class ClientManager extends EventEmitter {
  private clients: Map<string, ClientInfo> = new Map();  // 所有客户端
  private rooms: Map<string, RoomInfo> = new Map();      // 所有聊天室
  private messageHistory: Message[] = [];                // 消息历史
  private maxHistorySize = 1000;                         // 最大历史消息数
  
  constructor() {
    super();
    this.setupDefaultRoom();
  }
  
  // 添加客户端
  public addClient(socket: any, username: string, ip: string, port: number): string {
    const clientId = this.generateClientId(ip, port);
    
    const clientInfo: ClientInfo = {
      id: clientId,
      socket,
      username: username || `用户_${Date.now().toString(36)}`,
      ip,
      port,
      connectedAt: new Date(),
      lastActivity: new Date(),
      room: 'general',  // 默认加入公共聊天室
      status: 'online'
    };
    
    this.clients.set(clientId, clientInfo);
    
    // 添加到默认房间
    this.joinRoom(clientId, 'general');
    
    // 发出事件
    this.emit('clientConnected', clientInfo);
    this.emit('clientCountChanged', this.getClientCount());
    
    return clientId;
  }
  
  // 移除客户端
  public removeClient(clientId: string): boolean {
    const client = this.clients.get(clientId);
    if (!client) return false;
    
    // 从所有房间中移除
    for (const room of this.rooms.values()) {
      room.clients.delete(clientId);
    }
    
    this.clients.delete(clientId);
    
    // 发出事件
    this.emit('clientDisconnected', client);
    this.emit('clientCountChanged', this.getClientCount());
    
    return true;
  }
  
  // 获取客户端信息
  public getClient(clientId: string): ClientInfo | undefined {
    return this.clients.get(clientId);
  }
  
  // 根据用户名查找客户端
  public getClientByUsername(username: string): ClientInfo | undefined {
    for (const client of this.clients.values()) {
      if (client.username === username) {
        return client;
      }
    }
    return undefined;
  }
  
  // 更新客户端活动时间
  public updateClientActivity(clientId: string): void {
    const client = this.clients.get(clientId);
    if (client) {
      client.lastActivity = new Date();
    }
  }
  
  // 更新用户名
  public updateUsername(clientId: string, newUsername: string): string | null {
    const client = this.clients.get(clientId);
    if (!client) return null;
    
    const oldUsername = client.username;
    
    // 检查用户名是否已被使用
    if (newUsername !== oldUsername) {
      for (const otherClient of this.clients.values()) {
        if (otherClient.id !== clientId && otherClient.username === newUsername) {
          return '用户名已被使用';
        }
      }
    }
    
    client.username = newUsername;
    
    // 发出用户名变更事件
    this.emit('usernameChanged', { clientId, oldUsername, newUsername });
    
    return null; // 没有错误
  }
  
  // 发送消息到指定客户端
  public sendToClient(clientId: string, message: Partial<Message>): boolean {
    const client = this.clients.get(clientId);
    if (!client || !client.socket) return false;
    
    const fullMessage: Message = {
      id: this.generateMessageId(),
      type: message.type || 'chat',
      from: message.from || 'system',
      to: message.to,
      room: message.room,
      content: message.content || '',
      timestamp: new Date(),
      status: 'sent'
    };
    
    // 添加到历史记录
    this.addToHistory(fullMessage);
    
    try {
      // 发送消息（根据socket类型决定发送方式）
      if (client.socket.write) {
        // TCP socket
        const data = JSON.stringify(fullMessage);
        client.socket.write(data + '\n');
      } else if (client.socket.send) {
        // WebSocket
        client.socket.send(JSON.stringify(fullMessage));
      }
      
      // 更新消息状态为已送达
      setTimeout(() => {
        fullMessage.status = 'delivered';
        this.emit('messageDelivered', fullMessage);
      }, 100);
      
      return true;
    } catch (error) {
      console.error(`发送消息到客户端 ${clientId} 失败:`, error);
      return false;
    }
  }
  
  // 广播消息到所有客户端
  public broadcast(message: Partial<Message>, excludeClientId?: string): number {
    let sentCount = 0;
    
    for (const clientId of this.clients.keys()) {
      if (clientId === excludeClientId) continue;
      
      if (this.sendToClient(clientId, message)) {
        sentCount++;
      }
    }
    
    return sentCount;
  }
  
  // 广播消息到指定房间
  public broadcastToRoom(roomName: string, message: Partial<Message>, excludeClientId?: string): number {
    const room = this.rooms.get(roomName);
    if (!room) return 0;
    
    let sentCount = 0;
    
    for (const clientId of room.clients) {
      if (clientId === excludeClientId) continue;
      
      const roomMessage = { ...message, room: roomName };
      if (this.sendToClient(clientId, roomMessage)) {
        sentCount++;
      }
    }
    
    return sentCount;
  }
  
  // 发送私聊消息
  public sendPrivateMessage(fromClientId: string, toUsername: string, content: string): boolean {
    const fromClient = this.clients.get(fromClientId);
    if (!fromClient) return false;
    
    const toClient = this.getClientByUsername(toUsername);
    if (!toClient) {
      // 通知发送者目标用户不存在
      this.sendToClient(fromClientId, {
        type: 'system',
        from: 'system',
        content: `用户 ${toUsername} 不在线`
      });
      return false;
    }
    
    // 发送给接收者
    const privateMessage: Partial<Message> = {
      type: 'private',
      from: fromClient.username,
      to: toClient.username,
      content,
      timestamp: new Date()
    };
    
    this.sendToClient(toClient.id, privateMessage);
    
    // 也发送给发送者（作为确认）
    this.sendToClient(fromClientId, privateMessage);
    
    this.emit('privateMessage', {
      from: fromClientId,
      to: toClient.id,
      content
    });
    
    return true;
  }
  
  // 房间管理
  public createRoom(roomName: string, createdBy: string, isPrivate = false): boolean {
    if (this.rooms.has(roomName)) return false;
    
    const roomInfo: RoomInfo = {
      name: roomName,
      clients: new Set(),
      createdBy,
      createdAt: new Date(),
      isPrivate
    };
    
    this.rooms.set(roomName, roomInfo);
    this.emit('roomCreated', roomInfo);
    
    return true;
  }
  
  public joinRoom(clientId: string, roomName: string): boolean {
    const client = this.clients.get(clientId);
    const room = this.rooms.get(roomName);
    
    if (!client || !room) return false;
    
    // 离开当前房间
    if (client.room && client.room !== roomName) {
      this.leaveRoom(clientId, client.room);
    }
    
    // 加入新房间
    room.clients.add(clientId);
    client.room = roomName;
    
    this.emit('clientJoinedRoom', { clientId, roomName });
    
    // 通知房间内的其他用户
    this.broadcastToRoom(roomName, {
      type: 'system',
      from: 'system',
      content: `${client.username} 加入了房间`
    }, clientId);
    
    // 发送房间用户列表给新加入的用户
    this.sendRoomUserList(clientId, roomName);
    
    return true;
  }
  
  public leaveRoom(clientId: string, roomName: string): boolean {
    const client = this.clients.get(clientId);
    const room = this.rooms.get(roomName);
    
    if (!client || !room) return false;
    
    room.clients.delete(clientId);
    
    // 通知房间内的其他用户
    this.broadcastToRoom(roomName, {
      type: 'system',
      from: 'system',
      content: `${client.username} 离开了房间`
    });
    
    this.emit('clientLeftRoom', { clientId, roomName });
    
    // 如果房间空了并且不是默认房间，可以删除房间
    if (room.clients.size === 0 && roomName !== 'general') {
      this.deleteRoom(roomName);
    }
    
    return true;
  }
  
  private deleteRoom(roomName: string): void {
    if (roomName === 'general') return; // 不删除默认房间
    
    this.rooms.delete(roomName);
    this.emit('roomDeleted', roomName);
  }
  
  // 获取房间用户列表
  private sendRoomUserList(clientId: string, roomName: string): void {
    const room = this.rooms.get(roomName);
    if (!room) return;
    
    const users = Array.from(room.clients)
      .map(id => {
        const client = this.clients.get(id);
        return client ? client.username : '';
      })
      .filter(name => name !== '');
    
    this.sendToClient(clientId, {
      type: 'system',
      from: 'system',
      content: `房间 ${roomName} 当前用户 (${users.length}): ${users.join(', ')}`
    });
  }
  
  // 统计信息
  public getStats(): any {
    return {
      totalClients: this.clients.size,
      totalRooms: this.rooms.size,
      totalMessages: this.messageHistory.length,
      activeClients: Array.from(this.clients.values()).filter(
        client => Date.now() - client.lastActivity.getTime() < 5 * 60 * 1000
      ).length,
      rooms: Array.from(this.rooms.entries()).map(([name, room]) => ({
        name,
        clientCount: room.clients.size,
        isPrivate: room.isPrivate
      }))
    };
  }
  
  // 获取客户端列表
  public getClientList(): ClientInfo[] {
    return Array.from(this.clients.values());
  }
  
  // 获取在线用户列表
  public getOnlineUsers(): string[] {
    return Array.from(this.clients.values()).map(client => client.username);
  }
  
  // 获取客户端数量
  public getClientCount(): number {
    return this.clients.size;
  }
  
  // 清理不活跃的客户端
  public cleanupInactiveClients(inactiveMinutes = 10): string[] {
    const now = new Date();
    const inactiveThreshold = inactiveMinutes * 60 * 1000;
    const removedClients: string[] = [];
    
    for (const [clientId, client] of this.clients.entries()) {
      const inactiveTime = now.getTime() - client.lastActivity.getTime();
      
      if (inactiveTime > inactiveThreshold) {
        this.removeClient(clientId);
        removedClients.push(client.username);
      }
    }
    
    return removedClients;
  }
  
  // 生成唯一客户端ID
  private generateClientId(ip: string, port: number): string {
    const data = `${ip}:${port}:${Date.now()}:${Math.random()}`;
    return createHash('sha256').update(data).digest('hex').substring(0, 16);
  }
  
  // 生成消息ID
  private generateMessageId(): string {
    return `msg_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
  }
  
  // 添加消息到历史记录
  private addToHistory(message: Message): void {
    this.messageHistory.push(message);
    
    // 限制历史记录大小
    if (this.messageHistory.length > this.maxHistorySize) {
      this.messageHistory = this.messageHistory.slice(-this.maxHistorySize);
    }
    
    this.emit('messageAdded', message);
  }
  
  // 获取消息历史
  public getMessageHistory(count = 50): Message[] {
    return this.messageHistory.slice(-count);
  }
  
  // 获取房间消息历史
  public getRoomMessageHistory(roomName: string, count = 50): Message[] {
    return this.messageHistory
      .filter(msg => msg.room === roomName)
      .slice(-count);
  }
  
  // 初始化默认房间
  private setupDefaultRoom(): void {
    this.createRoom('general', 'system', false);
    this.createRoom('random', 'system', false);
  }
}