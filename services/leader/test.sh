#!/bin/bash

# This script runs a set of curl tests against the running 'leader' container.
# Make sure the container is running and accessible at localhost:8001 before running these tests.

API_URL="http://localhost:8001"

echo "=== Leader API Test Suite ==="
echo "API URL: $API_URL"
echo ""

# Test 1: Health check
echo "===> Test 1: Health check"
curl -i "$API_URL/health"
echo -e "\n"

# Test 2: Root endpoint
echo "===> Test 2: Root endpoint"
curl -i "$API_URL/"
echo -e "\n"

# Test 3: API documentation
echo "===> Test 3: API documentation"
curl -i "$API_URL/docs"
echo -e "\n"

# Test 4: POST /v1/imalive - Service health notification
echo "===> Test 4: POST /v1/imalive - Service health notification"
curl -i -X POST "$API_URL/v1/imalive" \
  -H "Content-Type: application/json" \
  -d '{
    "node_name": "test-web-service",
    "node_type": "web",
    "instance_key": "test-uuid-12345",
    "host": "192.168.1.100",
    "port": 8080,
    "additional": {
      "version": "1.0.0",
      "environment": "test"
    }
  }'
echo -e "\n"

# Test 5: POST /v1/imalive - Another service
echo "===> Test 5: POST /v1/imalive - Another service"
curl -i -X POST "$API_URL/v1/imalive" \
  -H "Content-Type: application/json" \
  -d '{
    "node_name": "test-db-service",
    "node_type": "database",
    "instance_key": "test-uuid-67890",
    "host": "192.168.1.101",
    "port": 5432,
    "additional": {
      "version": "13.0",
      "database": "postgresql"
    }
  }'
echo -e "\n"

# Test 6: GET /v1/list - List all registered services
echo "===> Test 6: GET /v1/list - List all registered services"
curl -i "$API_URL/v1/list"
echo -e "\n"

# Test 7: POST /v1/discover - Discover services
echo "===> Test 7: POST /v1/discover - Discover services"
curl -i -X POST "$API_URL/v1/discover" \
  -H "Content-Type: application/json" \
  -d '{
    "query": "web services"
  }'
echo -e "\n"

# Test 8: GET /v1/getenv - Get environment information
echo "===> Test 8: GET /v1/getenv - Get environment information"
curl -i "$API_URL/v1/getenv"
echo -e "\n"

# Test 9: POST /v1/imalive - Invalid request (missing required fields)
echo "===> Test 9: POST /v1/imalive - Invalid request (missing required fields)"
curl -i -X POST "$API_URL/v1/imalive" \
  -H "Content-Type: application/json" \
  -d '{
    "node_name": "test-service"
  }'
echo -e "\n"

# Test 10: POST /v1/imalive - Invalid request (wrong data types)
echo "===> Test 10: POST /v1/imalive - Invalid request (wrong data types)"
curl -i -X POST "$API_URL/v1/imalive" \
  -H "Content-Type: application/json" \
  -d '{
    "node_name": "test-service",
    "node_type": "web",
    "instance_key": "test-uuid",
    "host": 123,
    "port": "not-a-number"
  }'
echo -e "\n"

# Test 11: Non-existent endpoint to check 404 handling
echo "===> Test 11: Non-existent endpoint (should return 404)"
curl -i "$API_URL/doesnotexist"
echo -e "\n"

# Test 12: POST /v1/imalive - Minimal valid request
echo "===> Test 12: POST /v1/imalive - Minimal valid request"
curl -i -X POST "$API_URL/v1/imalive" \
  -H "Content-Type: application/json" \
  -d '{
    "node_name": "minimal-service",
    "node_type": "api",
    "instance_key": "minimal-uuid"
  }'
echo -e "\n"

echo "=== Test Suite Complete ==="
echo ""
echo "Summary:"
echo "- Tests 1-3: Basic endpoints (health, root, docs)"
echo "- Tests 4-5: Service registration via /v1/imalive"
echo "- Test 6: List registered services"
echo "- Test 7: Service discovery"
echo "- Test 8: Environment information"
echo "- Tests 9-10: Error handling (invalid requests)"
echo "- Test 11: 404 error handling"
echo "- Test 12: Minimal valid request"
echo ""
echo "Check the responses above to verify API functionality."
