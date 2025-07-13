const { createApp } = Vue;

createApp({
    data() {
        return {
            sidebarCollapsed: false,
            currentPage: 'dashboard',
            clusterReady: false,
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
        }
    },
    mounted() {
        // Initialize the dashboard
        console.log('Lunaricorn Dashboard initialized');
        // Check cluster status
        this.checkClusterStatus();
        // Check cluster status every 30 seconds
        setInterval(() => {
            this.checkClusterStatus();
        }, 3000);
    }
}).mount('#app');
