import net, { Server, Socket } from 'net';
import { EventEmitter } from 'events';
import { createHash } from 'crypto';

export interface ClientInfo {
  id: string;
  socket: Socket;
  username: string;
  ip: string;
  port: number;
  connectedAt: Date;
  lastActivity: Date;
}

export class TCPServer extends EventEmitter {
  private server: Server;  // Node.js的Server实例
  private clients: Map<string, ClientInfo> = new Map();
  private port: number;
  private isRunning: boolean = false;

  constructor(port: number = 8888) {
    super();
    this.port = port;
    this.server = net.createServer(this.handleConnection.bind(this));
    
    this.setupServerEvents();
  }

  private setupServerEvents(): void {
    this.server.on('error', (error: Error) => {
      this.emit('error', error);
    });

    this.server.on('close', () => {
      this.isRunning = false;
      this.emit('stopped');
    });
  }

  private handleConnection(socket: Socket): void {
    const clientId = this.generateClientId(socket);
    const clientInfo: ClientInfo = {
      id: clientId,
      socket,
      username: `User_${this.clients.size + 1}`,
      ip: socket.remoteAddress || 'unknown',
      port: socket.remotePort || 0,
      connectedAt: new Date(),
      lastActivity: new Date()
    };

    this.clients.set(clientId, clientInfo);
    this.setupSocketEvents(socket, clientId);
    
    this.emit('clientConnected', clientInfo);
  }

  private setupSocketEvents(socket: Socket, clientId: string): void {
    socket.on('data', (data: Buffer) => {
      this.handleClientData(clientId, data);
    });

    socket.on('end', () => {
      this.handleClientDisconnect(clientId);
    });

    socket.on('error', (error: Error) => {
      this.emit('clientError', clientId, error);
      this.handleClientDisconnect(clientId);
    });
  }

  private handleClientData(clientId: string, data: Buffer): void {
    const client = this.clients.get(clientId);
    if (!client) return;

    client.lastActivity = new Date();
    const message = data.toString().trim();
    
    if (message) {
      this.emit('message', clientId, message);
    }
  }

  private handleClientDisconnect(clientId: string): void {
    const client = this.clients.get(clientId);
    if (client) {
      this.clients.delete(clientId);
      this.emit('clientDisconnected', client);
    }
  }

  private generateClientId(socket: Socket): string {
    const data = `${socket.remoteAddress}:${socket.remotePort}:${Date.now()}`;
    return createHash('sha256').update(data).digest('hex').substring(0, 16);
  }

  public start(): Promise<void> {
    return new Promise((resolve, reject) => {
      if (this.isRunning) {
        reject(new Error('Server is already running'));
        return;
      }

      this.server.listen(this.port, () => {
        this.isRunning = true;
        this.emit('started', this.port);
        resolve();
      });

      this.server.once('error', reject);
    });
  }

  public stop(): void {
    if (this.isRunning) {
      for (const client of this.clients.values()) {
        client.socket.end('Server is shutting down\n');
      }
      this.clients.clear();
      this.server.close();
    }
  }

  public broadcast(message: string, excludeClientId?: string): void {
    for (const [clientId, client] of this.clients.entries()) {
      if (clientId !== excludeClientId) {
        client.socket.write(message + '\n');
      }
    }
  }

  public getClientCount(): number {
    return this.clients.size;
  }

  public getClientList(): ClientInfo[] {
    return Array.from(this.clients.values());
  }
}