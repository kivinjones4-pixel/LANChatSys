// src/FileServer.ts
import net, { Server, Socket } from 'net';
import { EventEmitter } from 'events';
import { createWriteStream, existsSync, mkdirSync } from 'fs';
import { join } from 'path';

export class FileServer extends EventEmitter {
    private server: Server;
    private clients: Map<string, Socket> = new Map();
    private uploads: Map<string, any> = new Map();
    private uploadDir: string;

    constructor(port: number = 8889, uploadDir: string = './uploads') {
        super();
        this.uploadDir = uploadDir;
        this.ensureUploadDir();
        this.server = net.createServer(this.handleConnection.bind(this));
        this.server.listen(port, () => {
            console.log(`文件服务器监听端口 ${port}`);
            console.log(`上传目录: ${uploadDir}`);
        });
    }

    private ensureUploadDir(): void {
        if (!existsSync(this.uploadDir)) {
            mkdirSync(this.uploadDir, { recursive: true });
        }
    }

    private handleConnection(socket: Socket): void {
        const clientId = `${socket.remoteAddress}:${socket.remotePort}`;
        this.clients.set(clientId, socket);

        console.log(`客户端连接: ${clientId}`);

        let buffer = Buffer.alloc(0);
        let currentUpload: any = null;

        socket.on('data', (data: Buffer) => {
            buffer = Buffer.concat([buffer, data]);

            // 检查文件传输标识
            if (buffer.includes('FILE_START')) {
                this.handleFileStart(socket, buffer);
                buffer = Buffer.alloc(0);
            } else if (buffer.includes('FILE_DATA') && currentUpload) {
                this.handleFileData(socket, buffer, currentUpload);
                buffer = Buffer.alloc(0);
            } else if (buffer.includes('IMAGE_MSG')) {
                this.handleImageMessage(socket, buffer);
                buffer = Buffer.alloc(0);
            } else {
                // 处理文本消息
                const message = buffer.toString().trim();
                if (message) {
                    this.broadcastMessage(clientId, message);
                }
                buffer = Buffer.alloc(0);
            }
        });

        socket.on('end', () => {
            console.log(`客户端断开: ${clientId}`);
            this.clients.delete(clientId);
        });

        socket.on('error', (err) => {
            console.error(`客户端错误 ${clientId}:`, err);
            this.clients.delete(clientId);
        });
    }

    private handleFileStart(socket: Socket, data: Buffer): void {
        const startIndex = data.indexOf('FILE_START') + 10;
        const fileData = data.slice(startIndex);

        // 解析文件头
        // 这里需要实现文件头解析逻辑
        console.log('接收到文件开始标识');
    }

    private handleFileData(socket: Socket, data: Buffer, upload: any): void {
        const startIndex = data.indexOf('FILE_DATA') + 9;
        const chunk = data.slice(startIndex);

        // 写入文件
        if (upload.writeStream) {
            upload.writeStream.write(chunk);
            upload.bytesReceived += chunk.length;

            // 发送进度给客户端
            const progress = Math.round((upload.bytesReceived / upload.fileSize) * 100);
            socket.write(`UPLOAD_PROGRESS:${progress}\n`);
        }
    }

    private handleImageMessage(socket: Socket, data: Buffer): void {
        const startIndex = data.indexOf('IMAGE_MSG') + 9;
        const imageData = data.slice(startIndex);

        // 广播图片消息给所有客户端
        this.clients.forEach((client, clientId) => {
            if (client !== socket) {
                client.write(data);
            }
        });
    }

    private broadcastMessage(senderId: string, message: string): void {
        this.clients.forEach((client, clientId) => {
            if (clientId !== senderId) {
                client.write(`${message}\n`);
            }
        });
    }

    public stop(): void {
        this.server.close();
        console.log('文件服务器已停止');
    }
}