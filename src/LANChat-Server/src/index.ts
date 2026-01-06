import net, { Socket } from 'net';
import readline from 'readline';

const PORT = 8888;
interface ClientInfo {
    socket: Socket;
    username: string;
    remoteAddress: string;
    remotePort: number;
    online: boolean; // 添加在线状态
    lastActive: Date; // 最后活动时间
}

const clients: Map<string, ClientInfo> = new Map();

const server = net.createServer((socket) => {
    const clientId = `${socket.remoteAddress}:${socket.remotePort}`;
    console.log(`🔗 客户端连接: ${clientId}`);
    
    const clientInfo: ClientInfo = {
        socket,
        username: `User${clients.size + 1}`,
        remoteAddress: socket.remoteAddress || 'unknown',
        remotePort: socket.remotePort || 0,
        online: true,
        lastActive: new Date()
    };
    
    clients.set(clientId, clientInfo);
    
    // 广播用户上线通知
    broadcastUserStatus(clientId, true);
    
    // 发送在线用户列表给新连接的用户
    sendUserListToClient(clientId);

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
        // 广播用户下线通知
        broadcastUserStatus(clientId, false);
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
            // 检查是否为私聊消息
            if (jsonData.target && jsonData.target !== '所有人') {
                handlePrivateMessage(client, jsonData, clientId);
                return;
            }
            
            // 普通文本消息（群聊）
            const content = jsonData.content || '';
            const time = jsonData.timestamp || new Date().toLocaleTimeString();
            
            console.log(`💬 ${sender}: ${content}`);
            broadcast(JSON.stringify({
                type: 'text',
                sender: sender,
                content: content,
                timestamp: time,
                isPrivate: false
            }), clientId);
            break;
        case 'private':
            // 私聊消息
            handlePrivateMessage(client, jsonData, clientId);
            break;
            
        // 在handleJsonMessage函数中，处理file_base64类型时：
        case 'file_base64':
        case 'image_base64': {
            const fileName = jsonData.filename || 'unknown';
            const fileSize = jsonData.filesize || 0;
            let base64Data = jsonData.filedata || '';
            
            // **更严格的Base64验证**
            if (!validateBase64(base64Data)) {
                console.error(`❌ Base64数据无效: ${fileName}`);
                
                // 发送错误消息给客户端
                const errorMsg = JSON.stringify({
                    type: 'error',
                    message: `文件 ${fileName} 数据格式错误`,
                    timestamp: new Date().toLocaleTimeString()
                });
                client.socket.write(errorMsg + '\n');
                return;
            }
            
            // 清理Base64数据
            base64Data = base64Data.replace(/\s+/g, '');
            
            // 确保Base64长度正确
            if (base64Data.length % 4 !== 0) {
                const padding = 4 - (base64Data.length % 4);
                base64Data += '='.repeat(padding);
            }
            
            // 验证文件大小
            const decodedSize = Buffer.from(base64Data, 'base64').length;
            if (fileSize > 0 && decodedSize !== fileSize) {
                console.warn(`⚠️ 文件大小不匹配: 声明${fileSize}字节，实际${decodedSize}字节`);
            }
            
            // 确保发送者信息存在
            if (!jsonData.sender) {
                jsonData.sender = client.username;
            }
            
            // 更新清理后的Base64数据
            jsonData.filedata = base64Data;
            
            if (type === 'image_base64') {
                console.log(`🖼️ ${sender} 发送了图片: ${fileName} (${formatBytes(decodedSize)})`);
            } else {
                console.log(`📁 ${sender} 发送了文件: ${fileName} (${formatBytes(decodedSize)})`);
            }
            
            // **发送前验证JSON**
            try {
                // 重新构建JSON确保格式正确
                const cleanJson = {
                    type: type,
                    sender: jsonData.sender,
                    filename: fileName,
                    filesize: fileSize,
                    filedata: base64Data,
                    timestamp: new Date().toLocaleTimeString()
                };
                
                // 如果是私聊文件，添加目标
                if ((jsonData as any).target) {
                    (cleanJson as any)['target'] = (jsonData as any).target;
                }
                
                const jsonString = JSON.stringify(cleanJson) + '\n';
                
                // 验证JSON长度（避免过大）
                if (jsonString.length > 10 * 1024 * 1024) { // 10MB限制
                    console.error(`❌ JSON太大: ${jsonString.length}字节`);
                    return;
                }
                
                // 广播给所有客户端
                broadcast(jsonString, clientId);
            } catch (err) {
                console.error(`❌ JSON序列化失败:`, err);
            }
            break;
        }
            
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
function handlePrivateMessage(client: ClientInfo, jsonData: any, clientId: string): void {
    const targetUsername = jsonData.target;
    const sender = jsonData.sender || client.username;
    const content = jsonData.content || '';
    
    if (!targetUsername || targetUsername === '所有人') {
        // 如果没有指定目标或目标是所有人，按普通消息处理
        handleJsonMessage(client, jsonData, clientId);
        return;
    }
    
    // 查找目标用户
    let targetClient: ClientInfo | null = null;
    let targetClientId: string = '';
    
    for (const [id, info] of clients.entries()) {
        if (info.username === targetUsername && info.online) {
            targetClient = info;
            targetClientId = id;
            break;
        }
    }
    
    if (!targetClient) {
        // 目标用户不在线，发送错误消息给发送者
        const errorMsg = JSON.stringify({
            type: 'error',
            message: `用户 ${targetUsername} 不在线或不存在`,
            timestamp: new Date().toLocaleTimeString()
        });
        client.socket.write(errorMsg + '\n');
        return;
    }
    
    if (targetClientId === clientId) {
        // 不能给自己发私聊
        const errorMsg = JSON.stringify({
            type: 'error',
            message: '不能给自己发送私聊消息',
            timestamp: new Date().toLocaleTimeString()
        });
        client.socket.write(errorMsg + '\n');
        return;
    }
    
    // 构建私聊消息
    const privateMessage = JSON.stringify({
        type: 'private',
        sender: sender,
        target: targetUsername,
        content: content,
        timestamp: new Date().toLocaleTimeString(),
        isOnline: true
    });
    
    // 发送给目标用户
    targetClient.socket.write(privateMessage + '\n');
    
    // 同时发送给发送者（显示在自己聊天窗口）
    client.socket.write(privateMessage + '\n');
    
    console.log(`💌 私聊 ${sender} -> ${targetUsername}: ${content}`);
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
    for (const [clientId, client] of clients.entries()) {
        sendUserListToClient(clientId);
    }
}

// 发送用户列表给特定客户端
function sendUserListToClient(clientId: string): void {
    const client = clients.get(clientId);
    if (!client) return;
    
    const userList = Array.from(clients.values())
        .map(c => ({
            username: c.username,
            online: c.online,
            isSelf: c.username === client.username
        }));
    
    try {
        client.socket.write(JSON.stringify({
            type: 'user_list',
            users: userList,
            timestamp: new Date().toLocaleTimeString()
        }) + '\n');
    } catch (err) {
        console.error(`发送用户列表失败 ${client.username}:`, err);
    }
}

function broadcast(message: string, excludeClientId?: string): void {
    try {
        const jsonData = JSON.parse(message);
        // 如果是私聊消息，不广播
        if (jsonData.type === 'private') {
            return;
        }
    } catch (error) {
        // 非JSON消息，正常广播
    }
    
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
// 在服务器端添加Base64验证函数
function validateBase64(base64Data: string): boolean {
    // 检查是否为有效的Base64
    if (!base64Data) return false;
    
    // 移除空白字符
    base64Data = base64Data.replace(/\s+/g, '');
    
    // Base64长度应该是4的倍数
    if (base64Data.length % 4 !== 0) return false;
    
    // Base64应该只包含合法字符
    const base64Regex = /^[A-Za-z0-9+/]*={0,2}$/;
    return base64Regex.test(base64Data);
}
// 用户状态广播函数
function broadcastUserStatus(clientId: string, isOnline: boolean): void {
    const client = clients.get(clientId);
    if (!client) return;
    
    const statusMessage = JSON.stringify({
        type: 'user_status',
        username: client.username,
        online: isOnline,
        timestamp: new Date().toLocaleTimeString()
    });
    
    broadcast(statusMessage, clientId);
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