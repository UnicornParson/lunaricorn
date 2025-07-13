from fastapi import APIRouter

router = APIRouter()

@router.get("/")
async def get_items():
    return [
        {"id": 1, "name": "Item A"},
        {"id": 2, "name": "Item B"}
    ]

@router.get("/{item_id}")
async def get_item(item_id: int):
    return {"id": item_id, "name": f"Item {item_id}"}