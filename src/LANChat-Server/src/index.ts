// index.ts (修复版)
import net, { Socket } from 'net';
import readline from 'readline';

const PORT = 8888;
interface ClientInfo {
    socket: Socket;
    username: string;
    remoteAddress: string;
    remotePort: number;
}

const clients: Map<string, ClientInfo> = new Map();

const server = net.createServer((socket) => {
    const clientId = `${socket.remoteAddress}:${socket.remotePort}`;
    console.log(`🔗 客户端连接: ${clientId}`);
    
    const clientInfo: ClientInfo = {
        socket,
        username: `User${clients.size + 1}`,
        remoteAddress: socket.remoteAddress || 'unknown',
        remotePort: socket.remotePort || 0
    };
    
    clients.set(clientId, clientInfo);
    
    // 发送欢迎消息
    socket.write('[系统] 欢迎使用局域网聊天室！请设置用户名\n');
    
    socket.on('data', (data: Buffer) => {
        const message = data.toString().trim();
        
        // 尝试解析JSON消息
        try {
            const jsonData = JSON.parse(message);
            handleJsonMessage(clientInfo, jsonData, clientId);
        } catch (error) {
            // 不是JSON，按文本处理
            handleTextMessage(clientInfo, message, clientId);
        }
    });
    
    socket.on('end', () => {
        console.log(`🔌 客户端断开: ${clientInfo.username} (${clientId})`);
        clients.delete(clientId);
        broadcast(`[系统] ${clientInfo.username} 离开了聊天室`, clientId);
    });
    
    socket.on('error', (err) => {
        console.error(`❌ 客户端错误 ${clientInfo.username}:`, err.message);
        clients.delete(clientId);
    });
});

function handleJsonMessage(client: ClientInfo, jsonData: any, clientId: string): void {
    const type = jsonData.type || 'text';
    // 优先使用消息中的sender，如果没有则使用客户端的用户名
    const sender = jsonData.sender || client.username;
    
    switch (type) {
        case 'text':
            // 广播文本消息
            const content = jsonData.content || '';
            const time = jsonData.timestamp || new Date().toLocaleTimeString();
            
            console.log(`💬 ${sender}: ${content}`);
            broadcast(JSON.stringify({
                type: 'text',
                sender: sender,
                content: content,
                timestamp: time
            }), clientId);
            break;
            
        case 'file_base64':
        case 'image_base64':
            // 广播文件消息 - 简化日志输出
            const fileName = jsonData.filename || 'unknown';
            const fileSize = jsonData.filesize || 0;
            
            // 只显示文件名和大小，不显示完整JSON
            if (type === 'image_base64') {
                console.log(`🖼️ ${sender} 发送了图片: ${fileName} (${formatBytes(fileSize)})`);
            } else {
                console.log(`📁 ${sender} 发送了文件: ${fileName} (${formatBytes(fileSize)})`);
            }
            
            // 确保发送者信息存在
            if (!jsonData.sender) {
                jsonData.sender = client.username;
            }
            
            // 直接转发JSON数据给所有客户端
            broadcast(JSON.stringify(jsonData) + '\n', clientId);
            break;
            
        case 'login':
            // 处理登录
            const username = jsonData.username || client.username;
            const oldUsername = client.username;
            client.username = username;
            
            console.log(`👤 用户登录: ${username} (${clientId})`);
            broadcast(`[系统] ${oldUsername} 更名为 ${username}\n`, clientId);
            break;
            
        default:
            console.log(`❓ 未知JSON类型: ${type}`);
    }
}

// 辅助函数：格式化文件大小
function formatBytes(bytes: number): string {
    if (bytes === 0) return '0 Bytes';
    
    const k = 1024;
    const sizes = ['Bytes', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

function handleTextMessage(client: ClientInfo, message: string, clientId: string): void {
    if (!message) return;
    
    // 处理登录消息
    if (message.startsWith('LOGIN:')) {
        const username = message.substring(6).trim();
        const oldUsername = client.username;
        client.username = username || client.username;
        
        console.log(`👤 用户登录: ${client.username} (${clientId})`);
        client.socket.write(`[系统] 欢迎 ${client.username}！\n`);
        broadcast(`[系统] ${oldUsername} 加入了聊天室\n`, clientId);
        
        // 发送在线用户列表给所有客户端
        sendUserListToAll();
        
        return;
    }
    
    // 处理聊天消息
    if (message.startsWith('CHAT:')) {
        const colonIndex = message.indexOf(':', 5);
        if (colonIndex !== -1) {
            const msgUsername = message.substring(5, colonIndex);
            const msgContent = message.substring(colonIndex + 1);
            
            console.log(`💬 ${msgUsername}: ${msgContent}`);
            
            // 广播给所有客户端
            broadcast(`[${new Date().toLocaleTimeString()}] ${msgUsername}: ${msgContent}\n`, clientId);
        }
        return;
    }
    
    // 处理USERS命令（客户端请求用户列表）
    if (message === 'USERS' || message.trim() === 'USERS') {
        sendUserListToClient(clientId);
        return;
    }
    
    // 普通文本消息
    console.log(`💬 ${client.username}: ${message}`);
    broadcast(`[${new Date().toLocaleTimeString()}] ${client.username}: ${message}\n`, clientId);
}

// 发送用户列表给所有客户端
function sendUserListToAll(): void {
    const userList = Array.from(clients.values())
        .map(c => c.username)
        .join(', ');
    
    for (const [clientId, client] of clients.entries()) {
        try {
            client.socket.write(`在线用户: ${userList}\n`);
        } catch (err) {
            console.error(`发送用户列表失败 ${client.username}:`, err);
        }
    }
}

// 发送用户列表给特定客户端
function sendUserListToClient(clientId: string): void {
    const client = clients.get(clientId);
    if (!client) return;
    
    const userList = Array.from(clients.values())
        .map(c => c.username)
        .join(', ');
    
    try {
        client.socket.write(`在线用户: ${userList}\n`);
    } catch (err) {
        console.error(`发送用户列表失败 ${client.username}:`, err);
    }
}

function broadcast(message: string, excludeClientId?: string): void {
    for (const [clientId, client] of clients.entries()) {
        if (clientId !== excludeClientId) {
            try {
                client.socket.write(message);
            } catch (err) {
                console.error(`广播消息失败 ${client.username}:`, err);
            }
        }
    }
}

// 启动服务器
server.listen(PORT, () => {
    console.log(`🚀 聊天服务器启动，监听端口 ${PORT}`);
    console.log('💡 服务器现在支持JSON格式消息\n');
});

// 命令行界面保持不变
const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout
});

rl.on('line', (input) => {
    const command = input.trim();
    
    if (command === '/users') {
        console.log(`在线客户端: ${clients.size}`);
        clients.forEach((client, id) => {
            console.log(`  ${client.username} (${id})`);
        });
    } else if (command === '/stop') {
        console.log('🛑 正在关闭服务器...');
        broadcast('[系统] 服务器即将关闭\n');
        
        setTimeout(() => {
            server.close();
            clients.forEach(client => client.socket.destroy());
            rl.close();
            process.exit(0);
        }, 1000);
    } else if (command.startsWith('/say ')) {
        const message = command.substring(5);
        broadcast(`[服务器公告] ${message}\n`);
        console.log(`📢 服务器公告: ${message}`);
    } else {
        console.log('❓ 未知命令。可用命令: /users, /stop, /say <消息>');
    }
});