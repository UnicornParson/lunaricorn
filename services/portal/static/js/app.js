const { createApp } = Vue;

createApp({
    data() {
        return {
            sidebarCollapsed: false,
            currentPage: 'dashboard',
            clusterReady: false,
            nodes: [],
            requiredNodes: [], // Add required nodes array
            menuItems: [
                {
                    id: 'dashboard',
                    title: 'Dashboard',
                    icon: 'fas fa-tachometer-alt',
                    href: '#dashboard'
                },
                {
                    id: 'nodes',
                    title: 'Nodes',
                    icon: 'fas fa-server',
                    href: '#nodes'
                },
                {
                    id: 'analytics',
                    title: 'Analytics',
                    icon: 'fas fa-chart-bar',
                    href: '#analytics'
                },
                {
                    id: 'settings',
                    title: 'Settings',
                    icon: 'fas fa-cog',
                    href: '#settings'
                },
                {
                    id: 'users',
                    title: 'Users',
                    icon: 'fas fa-users',
                    href: '#users'
                },
                {
                    id: 'logs',
                    title: 'Logs',
                    icon: 'fas fa-file-alt',
                    href: '#logs'
                }
            ]
        }
    },
    methods: {
        toggleSidebar() {
            this.sidebarCollapsed = !this.sidebarCollapsed;
        },
        setCurrentPage(pageId) {
            this.currentPage = pageId;
        },
        getCurrentPageTitle() {
            const currentItem = this.menuItems.find(item => item.id === this.currentPage);
            return currentItem ? currentItem.title : 'Dashboard';
        },
        logout() {
            // Logout functionality
            alert('Logout functionality would be implemented here');
        },
        async checkClusterStatus() {
            try {
                const response = await fetch('/api/cluster/ready');
                const data = await response.json();
                this.clusterReady = data.status === 'ready';
            } catch (error) {
                console.error('Failed to check cluster status:', error);
                this.clusterReady = false;
            }
        },
        async loadNodes() {
            try {
                const response = await fetch('/api/cluster/nodes');
                const data = await response.json();
                
                if (data.status === 'success' && data.data) {
                    // Store required nodes
                    this.requiredNodes = data.data.required_nodes || [];
                    // Transform the data to match our expected format
                    this.nodes = this.transformNodesData(data.data);
                } else {
                    console.error('Failed to load nodes:', data.error);
                    this.nodes = [];
                    this.requiredNodes = [];
                }
            } catch (error) {
                console.error('Failed to load nodes:', error);
                this.nodes = [];
                this.requiredNodes = [];
            }
        },
        transformNodesData(clusterData) {
            // Transform the cluster data to our nodes format
            const nodes = [];
            
            // Handle different possible data structures
            if (clusterData.nodes_summary) {
                // If we have nodes_summary, use it
                Object.entries(clusterData.nodes_summary).forEach(([nodeName, status]) => {
                    // Only include nodes that are in required_nodes if we're on the nodes page
                    if (this.currentPage === 'nodes' && this.requiredNodes.length > 0) {
                        if (this.requiredNodes.includes(nodeName)) {
                            nodes.push({
                                name: nodeName,
                                type: 'node',
                                status: status === 'on' ? 'online' : 'offline',
                                key: nodeName
                            });
                        }
                    } else {
                        // On dashboard, show all nodes
                        nodes.push({
                            name: nodeName,
                            type: 'node',
                            status: status === 'on' ? 'online' : 'offline',
                            key: nodeName
                        });
                    }
                });
            } else if (Array.isArray(clusterData)) {
                // If it's an array, assume it's the nodes list
                clusterData.forEach(node => {
                    const nodeName = node.name || node.key;
                    // Only include nodes that are in required_nodes if we're on the nodes page
                    if (this.currentPage === 'nodes' && this.requiredNodes.length > 0) {
                        if (this.requiredNodes.includes(nodeName)) {
                            nodes.push({
                                name: nodeName,
                                type: node.type || 'node',
                                status: this.determineNodeStatus(node),
                                key: node.key || nodeName
                            });
                        }
                    } else {
                        // On dashboard, show all nodes
                        nodes.push({
                            name: nodeName,
                            type: node.type || 'node',
                            status: this.determineNodeStatus(node),
                            key: node.key || nodeName
                        });
                    }
                });
            }
            
            return nodes;
        },
        determineNodeStatus(node) {
            // Determine if a node is online based on various indicators
            if (node.status) {
                return node.status === 'online' || node.status === 'on' ? 'online' : 'offline';
            }
            
            // Check if node has recent activity (within last 30 seconds)
            if (node.last_update) {
                const now = Date.now() / 1000;
                const age = now - node.last_update;
                return age < 30 ? 'online' : 'offline';
            }
            
            // Default to offline if we can't determine
            return 'offline';
        }
    },
    mounted() {
        // Initialize the dashboard
        console.log('Lunaricorn Dashboard initialized');
        // Check cluster status
        this.checkClusterStatus();
        // Load nodes
        this.loadNodes();
        // Check cluster status every 30 seconds
        setInterval(() => {
            this.checkClusterStatus();
        }, 3000);
        // Load nodes every 10 seconds
        setInterval(() => {
            this.loadNodes();
        }, 10000);
    }
}).mount('#app');
