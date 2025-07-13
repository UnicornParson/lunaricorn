const { createApp } = Vue;

createApp({
    data() {
        return {
            sidebarCollapsed: false,
            currentPage: 'dashboard',
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
        }
    },
    mounted() {
        // Initialize the dashboard
        console.log('Lunaricorn Dashboard initialized');
    }
}).mount('#app');
