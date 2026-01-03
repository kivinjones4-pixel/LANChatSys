export interface ClientInfo {
  id: string;
  socket: any;
  username: string;
  ip: string;
  port: number;
  connectedAt: Date;
  lastActivity: Date;
  room?: string;
  status: 'online' | 'away' | 'busy';
}

export interface Message {
  id: string;
  type: 'chat' | 'system' | 'private' | 'file' | 'command';
  from: string;
  to?: string;
  room?: string;
  content: string;
  timestamp: Date;
  status: 'sent' | 'delivered' | 'read';
}

export interface RoomInfo {
  name: string;
  clients: Set<string>;
  createdBy: string;
  createdAt: Date;
  isPrivate: boolean;
}